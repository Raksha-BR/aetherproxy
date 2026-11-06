#include "aether/networking/HttpSession.hpp"

#include <iostream>
#include <utility>

namespace aether::networking {

HttpSession::HttpSession(tcp::socket socket)
    : socket_(std::move(socket)),
      resolver_(socket_.get_executor()),
      upstream_socket_(socket_.get_executor()) {
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
    auto self = shared_from_this();

    resolver_.async_resolve(
        "127.0.0.1",
        "9001",
        [self](
            beast::error_code error,
            tcp::resolver::results_type results
        ) {
            if (error) {
                std::cerr << "[AetherProxy] Resolve error: "
                          << error.message() << '\n';
                self->close();
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
    if (error) {
        std::cerr << "[AetherProxy] Backend connection error: "
                  << error.message() << '\n';
        close();
        return;
    }
    std::cerr << "[AetherProxy] Backend connected\n";

    forward_request();
}

void HttpSession::forward_request() {
    request_.set(http::field::host, "127.0.0.1:9001");

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
    if (error) {
        std::cerr << "[AetherProxy] Backend write error: "
                  << error.message() << '\n';
        close();
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
    if (error) {
        std::cerr << "[AetherProxy] Backend read error: "
                  << error.message() << '\n';
        close();
        return;
    }

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

    if (!response_.keep_alive()) {
        close();
        return;
    }

    request_ = {};
    response_ = {};
    upstream_buffer_.consume(upstream_buffer_.size());

    upstream_socket_ = tcp::socket(socket_.get_executor());

    read_request();
}

void HttpSession::close() {
    beast::error_code ignored;

    resolver_.cancel();

    upstream_socket_.close(ignored);

    socket_.shutdown(tcp::socket::shutdown_both, ignored);
    socket_.close(ignored);
}

} // namespace aether::networking