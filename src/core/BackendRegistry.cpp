
#include "aether/core/BackendRegistry.hpp"

#include <algorithm>
#include <limits>
#include <random>
#include <stdexcept>
#include <utility>

namespace aether::core {

BackendRegistry::BackendRegistry(
    std::vector<Backend> backends,
    LoadBalancingStrategy strategy
)
    : backends_(std::move(backends)),
      strategy_(strategy),
      current_weights_(backends_.size(), 0),
      random_engine_(std::random_device{}()) {

    if (backends_.empty()) {
        throw std::invalid_argument(
            "Backend registry cannot be empty."
        );
    }

    if (strategy_ != LoadBalancingStrategy::RoundRobin &&
    strategy_ != LoadBalancingStrategy::Random &&
    strategy_ != LoadBalancingStrategy::WeightedRoundRobin &&
    strategy_ != LoadBalancingStrategy::LeastConnections) {
        throw std::invalid_argument(
            "Invalid load-balancing strategy."
        );
    }

    const auto max_weight =
        static_cast<std::size_t>(
            std::numeric_limits<std::int64_t>::max()
        );

    std::size_t total_weight = 0;

    for (const auto& backend : backends_) {
        if (backend.weight == 0 || backend.weight > max_weight ||
            backend.weight > max_weight - total_weight) {
            throw std::invalid_argument(
                "Backend weights must be positive and their sum "
                "must fit in int64_t."
            );
        }

        total_weight += backend.weight;
    }
}

std::optional<Backend> BackendRegistry::acquire_backend() {
    std::lock_guard<std::mutex> lock(mutex_);

    std::vector<std::size_t> healthy_indices;

    for (std::size_t i = 0; i < backends_.size(); ++i) {
        if (backends_[i].healthy) {
            healthy_indices.push_back(i);
        } else {
            current_weights_[i] = 0;
        }
    }

    if (healthy_indices.empty()) {
        return std::nullopt;
    }

    std::size_t selected_index = healthy_indices.front();

    // Strategy 1: Round Robin
    if (strategy_ == LoadBalancingStrategy::RoundRobin) {
        for (std::size_t i = 0; i < backends_.size(); ++i) {
            const std::size_t index =
                (next_index_ + i) % backends_.size();

            if (backends_[index].healthy) {
                selected_index = index;
                next_index_ = (index + 1) % backends_.size();
                break;
            }
        }
    }

    // Strategy 2: Random
    else if (strategy_ == LoadBalancingStrategy::Random) {
        std::uniform_int_distribution<std::size_t> distribution(
            0,
            healthy_indices.size() - 1
        );

        selected_index =
            healthy_indices[distribution(random_engine_)];
    }

    // Strategy 3: Smooth Weighted Round Robin
    else if (strategy_ == LoadBalancingStrategy::WeightedRoundRobin) {
        std::size_t total_weight = 0;

        for (const auto index : healthy_indices) {
            const auto weight = backends_[index].weight;

            total_weight += weight;

            current_weights_[index] +=
                static_cast<std::int64_t>(weight);

            if (current_weights_[index] >
                current_weights_[selected_index]) {
                selected_index = index;
            }
        }

        current_weights_[selected_index] -=
            static_cast<std::int64_t>(total_weight);
    }

    // Strategy 4: Least Connections
    else if (strategy_ == LoadBalancingStrategy::LeastConnections) {
        std::size_t minimum_connections =
            backends_[healthy_indices.front()].active_connections;

        for (const auto index : healthy_indices) {
            minimum_connections = std::min(
                minimum_connections,
                backends_[index].active_connections
            );
        }

        // Round-robin tie-breaking among equally loaded backends.
        for (std::size_t i = 0; i < backends_.size(); ++i) {
            const std::size_t index =
                (next_index_ + i) % backends_.size();

            if (backends_[index].healthy &&
                backends_[index].active_connections ==
                    minimum_connections) {

                selected_index = index;
                next_index_ = (index + 1) % backends_.size();
                break;
            }
        }
    }

    ++backends_[selected_index].active_connections;

    return backends_[selected_index];
}

void BackendRegistry::release_backend(
    const std::string& host,
    const std::string& port
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host == host && backend.port == port) {
            if (backend.active_connections > 0) {
                --backend.active_connections;
            }

            return;
        }
    }
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
