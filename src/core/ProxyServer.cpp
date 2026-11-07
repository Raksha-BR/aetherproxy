#include "aether/core/ProxyServer.hpp"

#include <iostream>
#include <utility>
#include <vector>

namespace aether::core {

ProxyServer::ProxyServer(
    const std::string& address,
    unsigned short port
)
    : io_context_(1),
      registry_(
          std::make_shared<BackendRegistry>(
              std::vector<Backend>{
                  {"127.0.0.1", "9001"},
                  {"127.0.0.1", "9002"},
                  {"127.0.0.1", "9003"}
              }
          )
      ) {

    const auto endpoint =
        networking::tcp::endpoint(
            networking::net::ip::make_address(address),
            port
        );
        health_monitor_ =
            std::make_shared<BackendHealthMonitor>(
                io_context_,
                registry_
            );

    listener_ =
        std::make_shared<networking::HttpListener>(
            io_context_,
            endpoint,
            registry_
        );
}

void ProxyServer::run() {
    std::cout
        << "[AetherProxy] Listening on "
        << "127.0.0.1:8080\n";

    health_monitor_->start();
    listener_->run();

    io_context_.run();
}

} // namespace aether::core