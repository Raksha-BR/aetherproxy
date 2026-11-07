#include "aether/networking/HttpSession.hpp"
#include "aether/core/BackendRegistry.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace aether::networking {

HttpSession::HttpSession(
    tcp::socket socket,
    std::shared_ptr<aether::core::BackendRegistry> registry
)
    : registry_(std::move(registry)),
      socket_(std::move(socket)),
      resolver_(socket_.get_executor()),
      upstream_socket_(socket_.get_executor()),
      upstream_timer_(socket_.get_executor()) {
}

void HttpSession::run() {
    std::cerr << "[AetherProxy] Session started\n";
    read_request();
}

void HttpSession::read_request() {
    auto self = shared_from_this();

    http::async_read(
        socket_,
        buffer_,
        request_,
        [self](beast::error_code error, std::size_t bytes) {
            self->on_read(error, bytes);
        }
    );
}

void HttpSession::on_read(
    beast::error_code error,
    std::size_t
) {
    if (error == http::error::end_of_stream) {
        close();
        return;
    }

    if (error) {
        std::cerr << "[AetherProxy] Client read error: "
                  << error.message() << '\n';
        close();
        return;
    }

    std::cerr << "[AetherProxy] Client request received: "
              << request_.method_string() << " "
              << request_.target() << '\n';

    connect_backend();
}

void HttpSession::connect_backend() {
    auto backend = registry_->next_backend();

    if (!backend) {
        std::cerr << "[AetherProxy] No healthy backends available\n";

        send_error(
            http::status::service_unavailable,
            "No healthy backend servers available."
        );
        return;
    }

    current_backend_ = *backend;

    std::cerr << "[AetherProxy] Selected backend: "
              << current_backend_.host << ":"
              << current_backend_.port << '\n';

    start_upstream_timeout();

    auto self = shared_from_this();

    resolver_.async_resolve(
        current_backend_.host,
        current_backend_.port,
        [self](
            beast::error_code error,
            tcp::resolver::results_type results
        ) {
            if (self->response_started_) {
                return;
            }

            if (error) {
                std::cerr << "[AetherProxy] Backend resolve error: "
                          << error.message() << '\n';

                self->registry_->mark_unhealthy(
                    self->current_backend_.host,
                    self->current_backend_.port
                );

                self->send_error(
                    http::status::bad_gateway,
                    "Could not resolve backend address."
                );
                return;
            }

            std::cerr << "[AetherProxy] Backend address resolved\n";

            net::async_connect(
                self->upstream_socket_,
                results,
                [self](
                    beast::error_code connect_error,
                    const tcp::endpoint& endpoint
                ) {
                    self->on_connect(connect_error, endpoint);
                }
            );
        }
    );
}

void HttpSession::on_connect(
    beast::error_code error,
    const tcp::endpoint&
) {
    if (response_started_) {
        return;
    }

    if (error) {
        std::cerr << "[AetherProxy] Backend connection error: "
                  << error.message() << '\n';

        registry_->mark_unhealthy(
            current_backend_.host,
            current_backend_.port
        );
        send_error(
            http::status::bad_gateway,
            "Could not connect to backend server."
        );
        return;
    }

    std::cerr << "[AetherProxy] Backend connected\n";

    forward_request();
}

void HttpSession::forward_request() {
    request_.set(
    http::field::host,
    current_backend_.host + ":" + current_backend_.port
);

    auto self = shared_from_this();

    http::async_write(
        upstream_socket_,
        request_,
        [self](beast::error_code error, std::size_t bytes) {
            self->on_forward(error, bytes);
        }
    );
}

void HttpSession::on_forward(
    beast::error_code error,
    std::size_t
) {
    if (response_started_) {
        return;
    }

    if (error) {
        std::cerr << "[AetherProxy] Backend write error: "
                  << error.message() << '\n';

        registry_->mark_unhealthy(
            current_backend_.host,
            current_backend_.port
        );
        registry_->mark_unhealthy(
            current_backend_.host,
            current_backend_.port
        );
        send_error(
            http::status::bad_gateway,
            "Failed to forward request to backend."
        );
        return;
    }

    read_backend_response();
}

void HttpSession::read_backend_response() {
    auto self = shared_from_this();

    http::async_read(
        upstream_socket_,
        upstream_buffer_,
        response_,
        [self](beast::error_code error, std::size_t bytes) {
            self->on_backend_read(error, bytes);
        }
    );
}

void HttpSession::on_backend_read(
    beast::error_code error,
    std::size_t
) {
    if (response_started_) {
        return;
    }

    if (error) {
        std::cerr << "[AetherProxy] Backend read error: "
                  << error.message() << '\n';

        registry_->mark_unhealthy(
            current_backend_.host,
            current_backend_.port
        );
        send_error(
            http::status::bad_gateway,
            "Failed to receive response from backend."
        );
        return;
    }

    // The backend has responded. Stop the upstream timeout.
    cancel_upstream_timeout();

    // Keep the client connection policy independent
    // of the backend connection policy.
    response_.keep_alive(request_.keep_alive());

    beast::error_code ignored;

    upstream_socket_.shutdown(
        tcp::socket::shutdown_both,
        ignored
    );

    upstream_socket_.close(ignored);

    write_client_response();
}

void HttpSession::write_client_response() {
    auto self = shared_from_this();

    http::async_write(
        socket_,
        response_,
        [self](beast::error_code error, std::size_t bytes) {
            self->on_client_write(error, bytes);
        }
    );
}

void HttpSession::on_client_write(
    beast::error_code error,
    std::size_t
) {
    if (error) {
        std::cerr << "[AetherProxy] Client write error: "
                  << error.message() << '\n';
        close();
        return;
    }

    // Error responses always close the client connection.
    if (response_started_ || !response_.keep_alive()) {
        close();
        return;
    }

    request_ = {};
    response_ = {};

    upstream_buffer_.consume(upstream_buffer_.size());

    upstream_socket_ = tcp::socket(socket_.get_executor());

    read_request();
}

void HttpSession::start_upstream_timeout() {
    auto self = shared_from_this();

    upstream_timer_.expires_after(std::chrono::seconds(5));

    upstream_timer_.async_wait(
        [self](beast::error_code error) {
            if (error == net::error::operation_aborted) {
                return;
            }

            if (error || self->response_started_) {
                return;
            }

            std::cerr << "[AetherProxy] Backend request timed out\n";

            self->resolver_.cancel();

            beast::error_code ignored;
            self->upstream_socket_.close(ignored);

            self->send_error(
                http::status::gateway_timeout,
                "Backend server did not respond in time."
            );
        }
    );
}

void HttpSession::cancel_upstream_timeout() {
    beast::error_code ignored;
    upstream_timer_.cancel(ignored);
}

void HttpSession::send_error(
    http::status status,
    const std::string& message
) {
    // Prevent multiple callbacks from writing multiple responses.
    if (response_started_) {
        return;
    }

    response_started_ = true;

    cancel_upstream_timeout();

    response_ = {};

    response_.version(request_.version());
    response_.result(status);
    response_.set(http::field::server, "AetherProxy");
    response_.set(http::field::content_type, "text/plain");
    response_.keep_alive(false);

    response_.body() = message + "\n";
    response_.prepare_payload();

    std::cerr << "[AetherProxy] Sending HTTP "
              << static_cast<unsigned>(status)
              << " to client\n";

    write_client_response();
}

void HttpSession::close() {
    beast::error_code ignored;

    cancel_upstream_timeout();
    resolver_.cancel();

    upstream_socket_.close(ignored);

    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}

} // namespace aether::networking