#pragma once

#include "aether/core/BackendRegistry.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>

#include <memory>

namespace aether::networking {

namespace beast = boost::beast;
namespace net = boost::asio;

using tcp = net::ip::tcp;

class HttpListener
    : public std::enable_shared_from_this<HttpListener> {

public:
    HttpListener(
        net::io_context& io_context,
        const tcp::endpoint& endpoint,
        std::shared_ptr<aether::core::BackendRegistry> registry
    );

    void run();

private:
    void accept_connection();

    net::io_context& io_context_;

    std::shared_ptr<aether::core::BackendRegistry> registry_;

    tcp::acceptor acceptor_;
};

} // namespace aether::networking