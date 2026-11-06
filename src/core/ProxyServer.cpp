#include "aether/core/ProxyServer.hpp"

#include <iostream>

namespace aether::core {

ProxyServer::ProxyServer(
    const std::string& address,
    unsigned short port
)
    : io_context_(1) {

    const auto endpoint =
        networking::tcp::endpoint(
            networking::net::ip::make_address(address),
            port
        );

    listener_ =
        std::make_shared<networking::HttpListener>(
            io_context_,
            endpoint
        );
}

void ProxyServer::run() {
    std::cout
        << "[AetherProxy] Listening on "
        << "127.0.0.1:8080\n";

    listener_->run();

    io_context_.run();
}

} // namespace aether::core