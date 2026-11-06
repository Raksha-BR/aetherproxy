#pragma once

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <memory>

namespace aether::networking {

namespace beast = boost::beast;
namespace http = beast::http;
namespace net = boost::asio;

using tcp = net::ip::tcp;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    explicit HttpSession(tcp::socket socket);

    void run();

private:
    void read_request();

    void on_read(
        beast::error_code error,
        std::size_t bytes_transferred
    );

    void connect_backend();

    void on_connect(
        beast::error_code error,
        const tcp::endpoint& endpoint
    );

    void forward_request();

    void on_forward(
        beast::error_code error,
        std::size_t bytes_transferred
    );

    void read_backend_response();

    void on_backend_read(
        beast::error_code error,
        std::size_t bytes_transferred
    );

    void write_client_response();

    void on_client_write(
        beast::error_code error,
        std::size_t bytes_transferred
    );

    void close();

    tcp::socket socket_;
    tcp::resolver resolver_;
    tcp::socket upstream_socket_;

    beast::flat_buffer buffer_;
    beast::flat_buffer upstream_buffer_;

    http::request<http::string_body> request_;
    http::response<http::string_body> response_;
};

} // namespace aether::networking