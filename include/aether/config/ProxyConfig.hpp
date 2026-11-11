#pragma once

#include "aether/core/BackendRegistry.hpp"

#include <chrono>
#include <cstddef>
#include <string>
#include <vector>

namespace aether::config {

struct ProxyConfig {
    std::string server_host;
    unsigned short server_port;

    core::LoadBalancingStrategy load_balancing_strategy{
        core::LoadBalancingStrategy::RoundRobin
    };

    std::chrono::seconds health_check_interval;
    std::chrono::seconds health_check_timeout;

    std::chrono::seconds request_timeout{5};
    std::size_t max_retries{2};

    std::size_t failure_threshold;
    std::size_t success_threshold;

    std::vector<core::Backend> backends;

    static ProxyConfig load_from_file(const std::string& path);
};

} // namespace aether::config