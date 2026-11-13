#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <cstddef>

#include <memory>
#include <string>

#include "aether/core/BackendRegistry.hpp"
#include "aether/core/ProxyMetrics.hpp"
#include "aether/networking/BackendConnectionPool.hpp"

namespace aether::networking
{

    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;

    using tcp = net::ip::tcp;

    class HttpSession : public std::enable_shared_from_this<HttpSession>
    {

    public:
        explicit HttpSession(
            tcp::socket socket,
            std::shared_ptr<aether::core::BackendRegistry> registry,
            std::shared_ptr<BackendConnectionPool> connection_pool,
            std::chrono::seconds request_timeout,
            std::size_t max_retries,
            std::shared_ptr<aether::core::ProxyMetrics> metrics);

        void run();
        void stop();

    private:
        void read_request();

        void connect_backend();

        void forward_request();

        void on_read(
            beast::error_code error,
            std::size_t bytes_transferred);

        void on_forward(
            beast::error_code error,
            std::size_t bytes_transferred);

        void read_backend_response();

        void on_backend_read(
            beast::error_code error,
            std::size_t bytes_transferred);

        void write_client_response();

        void on_client_write(
            beast::error_code error,
            std::size_t bytes_transferred);

        void start_upstream_timeout();

        void cancel_upstream_timeout();

        void release_current_backend();

        void send_error(
            http::status status,
            const std::string &message);
        void record_request_success();
        void record_request_failure();

        void close();

        // Backend configuration and selection
        std::shared_ptr<aether::core::BackendRegistry> registry_;
        std::shared_ptr<aether::core::ProxyMetrics> metrics_;
        std::shared_ptr<BackendConnectionPool> connection_pool_;
        std::chrono::seconds request_timeout_;
        std::size_t max_retries_;
        std::size_t retries_attempted_{0};
        aether::core::Backend current_backend_;
        std::chrono::steady_clock::time_point request_started_at_;
        bool request_metric_recorded_{false};
        bool backend_acquired_{false};

        BackendConnectionPool::RequestId pool_request_id_{0};

        // Serializes all asynchronous handlers belonging to this session.
        net::strand<net::any_io_executor> strand_;
        // Client and upstream connections
        tcp::socket socket_;

        BackendConnectionPool::Connection upstream_connection_;
        // Upstream timeout management
        net::steady_timer upstream_timer_;

        // HTTP buffers
        beast::flat_buffer buffer_;
        beast::flat_buffer upstream_buffer_;

        // HTTP request and response
        http::request<http::string_body> request_;
        http::response<http::string_body> response_;

        // Prevent duplicate error responses
        bool response_started_{false};
        bool shutting_down_{false};
        bool request_in_progress_{false};
        bool connection_counted_{false};

        void on_pool_acquired(
            boost::system::error_code error,
            BackendConnectionPool::Connection connection);
    };

} // namespace aether::networking
