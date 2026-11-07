#pragma once

#include <cstddef>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace aether::core {

struct Backend {
    std::string host;
    std::string port;
    bool healthy{true};

    std::size_t consecutive_successes{0};
    std::size_t consecutive_failures{0};
};

class BackendRegistry {
public:
    explicit BackendRegistry(std::vector<Backend> backends);

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
    std::size_t next_index_{0};
    mutable std::mutex mutex_;
};

} // namespace aether::core