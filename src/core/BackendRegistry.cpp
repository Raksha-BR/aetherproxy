#include "aether/core/BackendRegistry.hpp"

#include <stdexcept>
#include <utility>

namespace aether::core {

BackendRegistry::BackendRegistry(std::vector<Backend> backends)
    : backends_(std::move(backends)) {

    if (backends_.empty()) {
        throw std::invalid_argument(
            "Backend registry cannot be empty."
        );
    }
}

std::optional<Backend> BackendRegistry::next_backend() {
    std::lock_guard<std::mutex> lock(mutex_);

    for (std::size_t i = 0; i < backends_.size(); ++i) {
        const std::size_t index =
            (next_index_ + i) % backends_.size();

        if (backends_[index].healthy) {
            next_index_ = (index + 1) % backends_.size();
            return backends_[index];
        }
    }

    return std::nullopt;
}

bool BackendRegistry::mark_healthy(
    const std::string& host,
    const std::string& port
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host == host && backend.port == port) {
            const bool changed = !backend.healthy;

            backend.healthy = true;
            backend.consecutive_successes = 0;
            backend.consecutive_failures = 0;

            return changed;
        }
    }

    return false;
}

bool BackendRegistry::mark_unhealthy(
    const std::string& host,
    const std::string& port
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host == host && backend.port == port) {
            const bool changed = backend.healthy;

            backend.healthy = false;
            backend.consecutive_successes = 0;
            backend.consecutive_failures = 0;

            return changed;
        }
    }

    return false;
}

std::optional<bool> BackendRegistry::record_health_check(
    const std::string& host,
    const std::string& port,
    bool success,
    std::size_t success_threshold,
    std::size_t failure_threshold
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host != host || backend.port != port) {
            continue;
        }

        if (success) {
            backend.consecutive_failures = 0;

            if (backend.healthy) {
                backend.consecutive_successes = 0;
                return std::nullopt;
            }

            ++backend.consecutive_successes;

            if (backend.consecutive_successes >= success_threshold) {
                backend.healthy = true;
                backend.consecutive_successes = 0;
                return true;
            }
        } else {
            backend.consecutive_successes = 0;

            if (!backend.healthy) {
                backend.consecutive_failures = 0;
                return std::nullopt;
            }

            ++backend.consecutive_failures;

            if (backend.consecutive_failures >= failure_threshold) {
                backend.healthy = false;
                backend.consecutive_failures = 0;
                return false;
            }
        }

        return std::nullopt;
    }

    return std::nullopt;
}

std::size_t BackendRegistry::size() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backends_.size();
}

std::size_t BackendRegistry::healthy_count() const {
    std::lock_guard<std::mutex> lock(mutex_);

    std::size_t count = 0;

    for (const auto& backend : backends_) {
        if (backend.healthy) {
            ++count;
        }
    }

    return count;
}

std::vector<Backend> BackendRegistry::all_backends() const {
    std::lock_guard<std::mutex> lock(mutex_);
    return backends_;
}

} // namespace aether::core