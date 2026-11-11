#include "aether/networking/HttpSession.hpp"
#include "aether/core/BackendRegistry.hpp"

#include <chrono>
#include <iostream>
#include <utility>

namespace aether::networking
{

    HttpSession::HttpSession(
        tcp::socket socket,
        std::shared_ptr<aether::core::BackendRegistry> registry,
        std::shared_ptr<BackendConnectionPool> connection_pool,
        std::chrono::seconds request_timeout,
        std::size_t max_retries,
        std::shared_ptr<aether::core::ProxyMetrics> metrics)
        : registry_(std::move(registry)),
          metrics_(std::move(metrics)),
          connection_pool_(std::move(connection_pool)),
          request_timeout_(request_timeout),
          max_retries_(max_retries),
          socket_(std::move(socket)),
          upstream_timer_(socket_.get_executor())
    {
    }

    void HttpSession::run()
    {
        if (metrics_)
        {
            metrics_->connection_opened();
            connection_counted_ = true;
        }

        std::cerr << "[AetherProxy] Session started\n";
        read_request();
    }

    void HttpSession::stop()
    {
        if (shutting_down_)
        {
            return;
        }

        shutting_down_ = true;

        // No request is being processed: this session is idle.
        if (!request_in_progress_)
        {
            std::cerr << "[AetherProxy] Closing idle client session\n";
            close();
        }
    }

    void HttpSession::read_request()
    {
        auto self = shared_from_this();

        http::async_read(
            socket_,
            buffer_,
            request_,
            [self](beast::error_code error, std::size_t bytes)
            {
                self->on_read(error, bytes);
            });
    }

    void HttpSession::on_read(
        beast::error_code error,
        std::size_t)
    {
        if (error == http::error::end_of_stream)
        {
            close();
            return;
        }

        if (error)
        {
            std::cerr << "[AetherProxy] Client read error: "
                      << error.message() << '\n';
            close();
            return;
        }

        request_in_progress_ = true;
        request_started_at_ = std::chrono::steady_clock::now();
        request_metric_recorded_ = false;

        if (metrics_)
        {
            metrics_->request_received();
        }
        std::cerr << "[AetherProxy] Client request received: "
                  << request_.method_string() << " "
                  << request_.target() << '\n';

        // Serve Prometheus metrics locally.
        if (request_.method() == http::verb::get &&
            request_.target() == "/metrics")
        {
            response_ = {};

            response_.version(request_.version());
            response_.result(http::status::ok);
            response_.set(
                http::field::server,
                "AetherProxy");
            response_.set(
                http::field::content_type,
                "text/plain; version=0.0.4; charset=utf-8");

            response_.keep_alive(request_.keep_alive());
            response_.body() = metrics_
                                   ? metrics_->render(*registry_, *connection_pool_)
                                   : "";
            response_.prepare_payload();

            write_client_response();
            return;
        }

        connect_backend();
    }

    void HttpSession::connect_backend()
    {
        auto backend = registry_->acquire_backend();

        if (!backend)
        {
            std::cerr << "[AetherProxy] No healthy backends available\n";

            send_error(
                http::status::service_unavailable,
                "No healthy backend servers available.");
            return;
        }

        current_backend_ = *backend;
        backend_acquired_ = true;

        std::cerr << "[AetherProxy] Selected backend: "
                  << current_backend_.host << ":"
                  << current_backend_.port << '\n';

        start_upstream_timeout();

        auto self = shared_from_this();

        pool_request_id_ = connection_pool_->async_acquire(
            current_backend_,
            [self](
                boost::system::error_code error,
                BackendConnectionPool::Connection connection)
            {
                self->pool_request_id_ = 0;
                self->on_pool_acquired(error, std::move(connection));
            });
    }

    void HttpSession::on_pool_acquired(
        boost::system::error_code error,
        BackendConnectionPool::Connection connection)
    {
        if (response_started_)
        {
            if (connection)
            {
                connection_pool_->release(
                    current_backend_,
                    std::move(connection),
                    false);
            }

            return;
        }

        if (error)
        {
            std::cerr << "[AetherProxy] Backend connection error: "
                      << error.message() << '\n';

            registry_->mark_unhealthy(
                current_backend_.host,
                current_backend_.port);

            release_current_backend();

            if (retries_attempted_ < max_retries_)
            {
                ++retries_attempted_;

                std::cerr << "[AetherProxy] Retrying backend connection ("
                          << retries_attempted_ << "/"
                          << max_retries_ << ")\n";

                connect_backend();
                return;
            }

            send_error(
                http::status::bad_gateway,
                "Could not connect to backend server.");
            return;
        }

        upstream_connection_ = std::move(connection);

        std::cerr << "[AetherProxy] Backend connection acquired from pool\n";

        forward_request();
    }

    void HttpSession::forward_request()
    {
        request_.set(
            http::field::host,
            current_backend_.host + ":" + current_backend_.port);

        auto self = shared_from_this();

        http::async_write(
            *upstream_connection_,
            request_,
            [self](beast::error_code error, std::size_t bytes)
            {
                self->on_forward(error, bytes);
            });
    }

    void HttpSession::on_forward(
        beast::error_code error,
        std::size_t)
    {
        if (response_started_)
        {
            return;
        }

        if (error)
        {
            std::cerr << "[AetherProxy] Backend write error: "
                      << error.message() << '\n';

            registry_->mark_unhealthy(
                current_backend_.host,
                current_backend_.port);
            send_error(
                http::status::bad_gateway,
                "Failed to forward request to backend.");
            return;
        }

        read_backend_response();
    }

    void HttpSession::read_backend_response()
    {
        auto self = shared_from_this();

        http::async_read(
            *upstream_connection_,
            upstream_buffer_,
            response_,
            [self](beast::error_code error, std::size_t bytes)
            {
                self->on_backend_read(error, bytes);
            });
    }

    void HttpSession::on_backend_read(
        beast::error_code error,
        std::size_t)
    {
        if (response_started_)
        {
            return;
        }

        if (error)
        {
            std::cerr << "[AetherProxy] Backend read error: "
                      << error.message() << '\n';

            registry_->mark_unhealthy(
                current_backend_.host,
                current_backend_.port);

            send_error(
                http::status::bad_gateway,
                "Failed to receive response from backend.");
            return;
        }

        cancel_upstream_timeout();

        // Determine whether the backend connection can be reused
        // before changing the response's client keep-alive policy.
        const bool reusable =
            request_.keep_alive() &&
            response_.keep_alive() &&
            upstream_connection_ &&
            upstream_connection_->is_open();

        if (upstream_connection_)
        {
            connection_pool_->release(
                current_backend_,
                std::move(upstream_connection_),
                reusable);
        }

        release_current_backend();

        // Apply the client's connection policy independently.
        response_.keep_alive(request_.keep_alive());

        write_client_response();
    }

    void HttpSession::write_client_response()
    {
        auto self = shared_from_this();

        http::async_write(
            socket_,
            response_,
            [self](beast::error_code error, std::size_t bytes)
            {
                self->on_client_write(error, bytes);
            });
    }

    void HttpSession::on_client_write(
        beast::error_code error,
        std::size_t)
    {
        if (error)
        {
            std::cerr << "[AetherProxy] Client write error: "
                      << error.message() << '\n';

            close();
            return;
        }

        // Error responses count as failed requests.
        if (response_started_)
        {
            record_request_failure();
            request_in_progress_ = false;
            close();
            return;
        }

        // A successfully forwarded response counts as success.
        record_request_success();

        if (!response_.keep_alive() || shutting_down_)
        {
            request_in_progress_ = false;
            close();
            return;
        }

        request_in_progress_ = false;
        retries_attempted_ = 0;
        request_ = {};
        response_ = {};

        upstream_buffer_.consume(upstream_buffer_.size());

        read_request();
    }

    void HttpSession::start_upstream_timeout()
    {
        auto self = shared_from_this();

        upstream_timer_.expires_after(request_timeout_);
        upstream_timer_.async_wait(
            [self](beast::error_code error)
            {
                if (error == net::error::operation_aborted)
                {
                    return;
                }

                if (error || self->response_started_)
                {
                    return;
                }

                std::cerr << "[AetherProxy] Backend request timed out\n";

                if (self->pool_request_id_ != 0)
                {
                    self->connection_pool_->cancel_acquire(
                        self->current_backend_,
                        self->pool_request_id_);

                    self->pool_request_id_ = 0;
                }

                self->send_error(
                    http::status::gateway_timeout,
                    "Backend server did not respond in time.");
            });
    }

    void HttpSession::cancel_upstream_timeout()
    {
        beast::error_code ignored;
        upstream_timer_.cancel(ignored);
    }

    void HttpSession::release_current_backend()
    {
        if (pool_request_id_ != 0)
        {
            connection_pool_->cancel_acquire(
                current_backend_,
                pool_request_id_);
            pool_request_id_ = 0;
        }
        // Any connection still held here was not returned for reuse.
        if (upstream_connection_)
        {
            connection_pool_->release(
                current_backend_,
                std::move(upstream_connection_),
                false);
        }

        if (!backend_acquired_)
        {
            return;
        }

        registry_->release_backend(
            current_backend_.host,
            current_backend_.port);

        backend_acquired_ = false;
    }
    void HttpSession::record_request_success()
    {
        if (!request_in_progress_ || request_metric_recorded_)
        {
            return;
        }

        if (metrics_)
        {
            metrics_->request_succeeded(
                std::chrono::steady_clock::now() - request_started_at_);
        }

        request_metric_recorded_ = true;
    }

    void HttpSession::record_request_failure()
    {
        if (!request_in_progress_ || request_metric_recorded_)
        {
            return;
        }

        if (metrics_)
        {
            metrics_->request_failed(
                std::chrono::steady_clock::now() - request_started_at_);
        }

        request_metric_recorded_ = true;
    }

    void HttpSession::send_error(
        http::status status,
        const std::string &message)
    {
        // Prevent multiple callbacks from writing multiple responses.
        if (response_started_)
        {
            return;
        }

        response_started_ = true;

        cancel_upstream_timeout();
        release_current_backend();

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

    void HttpSession::close()
    {
        record_request_failure();

        request_in_progress_ = false;
        beast::error_code ignored;

        cancel_upstream_timeout();
        release_current_backend();

        if (connection_counted_)
        {
            if (metrics_)
            {
                metrics_->connection_closed();
            }

            connection_counted_ = false;
        }

        socket_.shutdown(tcp::socket::shutdown_both, ignored);
        socket_.close(ignored);
    }

} // namespace aether::networking
