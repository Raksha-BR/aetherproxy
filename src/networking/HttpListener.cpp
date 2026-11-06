#include "aether/networking/HttpListener.hpp"

#include <iostream>
#include <utility>

namespace aether::networking {

HttpListener::HttpListener(
    net::io_context& io_context,
    const tcp::endpoint& endpoint
)
    : io_context_(io_context),
      acceptor_(io_context) {

    beast::error_code error;

    acceptor_.open(endpoint.protocol(), error);

    if (error) {
        throw beast::system_error(error);
    }

    acceptor_.set_option(
        net::socket_base::reuse_address(true),
        error
    );

    if (error) {
        throw beast::system_error(error);
    }

    acceptor_.bind(endpoint, error);

    if (error) {
        throw beast::system_error(error);
    }

    acceptor_.listen(
        net::socket_base::max_listen_connections,
        error
    );

    if (error) {
        throw beast::system_error(error);
    }
}

void HttpListener::run() {
    accept_connection();
}

void HttpListener::accept_connection() {
    auto self = shared_from_this();

    acceptor_.async_accept(
        io_context_,
        [self](
            beast::error_code error,
            tcp::socket socket
        ) {
            if (!error) {
                std::cerr << "[AetherProxy] Accepted client connection\n";

                std::make_shared<HttpSession>(
                    std::move(socket)
                )->run();
            } else {
                std::cerr
                    << "[AetherProxy] Accept error: "
                    << error.message() << '\n';
            }

            self->accept_connection();
        }
    );
}

} // namespace aether::networking