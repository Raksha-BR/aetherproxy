#pragma once

#include <atomic>
#include <chrono>
#include <cstdint>
#include <string>
#include "aether/networking/BackendConnectionPool.hpp"

namespace aether::core {


class BackendRegistry;
class ProxyMetrics {
public:
    void connection_opened();
    void connection_closed();

    void request_received();

    void request_succeeded(
        std::chrono::steady_clock::duration duration);

    void request_failed(
        std::chrono::steady_clock::duration duration);

std::string render(
    const BackendRegistry& registry,
    const aether::networking::BackendConnectionPool& connection_pool
) const;

private:
    std::atomic<std::uint64_t> active_connections_{0};
    std::atomic<std::uint64_t> requests_total_{0};
    std::atomic<std::uint64_t> requests_successful_{0};
    std::atomic<std::uint64_t> requests_failed_{0};

    std::atomic<std::uint64_t> request_duration_count_{0};
    std::atomic<std::uint64_t> request_duration_nanoseconds_{0};
};

} // namespace aether::core