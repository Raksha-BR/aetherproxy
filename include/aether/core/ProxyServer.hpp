#pragma once

#include "aether/config/ProxyConfig.hpp"
#include "aether/core/BackendHealthMonitor.hpp"
#include "aether/core/BackendRegistry.hpp"
#include "aether/networking/HttpListener.hpp"

#include <memory>

namespace aether::core {

class ProxyServer {
public:
    explicit ProxyServer(const config::ProxyConfig& config);

    void run();

private:
    networking::net::io_context io_context_;

    std::shared_ptr<BackendRegistry> registry_;
    std::shared_ptr<BackendHealthMonitor> health_monitor_;
    std::shared_ptr<networking::HttpListener> listener_;
};

} // namespace aether::core