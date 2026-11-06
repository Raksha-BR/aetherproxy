#pragma once

#include "aether/networking/HttpSession.hpp"

#include <memory>

namespace aether::networking {

class HttpListener
    : public std::enable_shared_from_this<HttpListener> {

public:
    HttpListener(
        net::io_context& io_context,
        const tcp::endpoint& endpoint
    );

    void run();

private:
    void accept_connection();

    net::io_context& io_context_;

    tcp::acceptor acceptor_;
};

} // namespace aether::networking