#pragma once

#include "aether/core/BackendRegistry.hpp"
#include "aether/networking/HttpListener.hpp"
#include "aether/core/BackendHealthMonitor.hpp"

#include <memory>
#include <string>

namespace aether::core {

class ProxyServer {
public:
    ProxyServer(
        const std::string& address,
        unsigned short port
    );

    void run();

private:
    networking::net::io_context io_context_;

    std::shared_ptr<BackendRegistry> registry_;
    std::shared_ptr<BackendHealthMonitor> health_monitor_;

    std::shared_ptr<networking::HttpListener> listener_;
};

} // namespace aether::core