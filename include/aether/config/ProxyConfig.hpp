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

    std::chrono::seconds health_check_interval;
    std::chrono::seconds health_check_timeout;

    std::size_t failure_threshold;
    std::size_t success_threshold;

    std::vector<core::Backend> backends;

    static ProxyConfig load_from_file(const std::string& path);
};

} // namespace aether::config