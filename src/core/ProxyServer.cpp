#include "aether/core/ProxyServer.hpp"

#include <iostream>
#include <utility>

namespace aether::core {

ProxyServer::ProxyServer(const config::ProxyConfig& config)
    : io_context_(1),
      registry_(
          std::make_shared<BackendRegistry>(config.backends)
      ) {

    const auto endpoint =
        networking::tcp::endpoint(
            networking::net::ip::make_address(config.server_host),
            config.server_port
        );

    health_monitor_ =
        std::make_shared<BackendHealthMonitor>(
            io_context_,
            registry_,
            config.health_check_interval,
            config.health_check_timeout,
            config.success_threshold,
            config.failure_threshold
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
        << "[AetherProxy] Starting configured server...\n";

    health_monitor_->start();
    listener_->run();

    io_context_.run();
}

} // namespace aether::core