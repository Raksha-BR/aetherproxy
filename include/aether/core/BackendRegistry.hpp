#pragma once

#include <cstddef>
#include <cstdint>
#include <mutex>
#include <optional>
#include <random>
#include <string>
#include <vector>

namespace aether::core {

enum class LoadBalancingStrategy {
    RoundRobin,
    Random,
    WeightedRoundRobin
};

struct Backend {
    std::string host;
    std::string port;
    std::size_t weight{1};
    bool healthy{true};

    std::size_t consecutive_successes{0};
    std::size_t consecutive_failures{0};
};

class BackendRegistry {
public:
    explicit BackendRegistry(
        std::vector<Backend> backends,
        LoadBalancingStrategy strategy =
            LoadBalancingStrategy::RoundRobin
    );

    std::optional<Backend> next_backend();

    bool mark_healthy(const std::string& host, const std::string& port);
    bool mark_unhealthy(const std::string& host, const std::string& port);

    std::optional<bool> record_health_check(
        const std::string& host,
        const std::string& port,
        bool success,
        std::size_t success_threshold,
        std::size_t failure_threshold
    );

    std::size_t size() const;
    std::size_t healthy_count() const;
    std::vector<Backend> all_backends() const;

private:
    std::vector<Backend> backends_;
    LoadBalancingStrategy strategy_;
    std::vector<std::int64_t> current_weights_;
    std::mt19937 random_engine_;
    std::size_t next_index_{0};

    mutable std::mutex mutex_;
};

} // namespace aether::core
