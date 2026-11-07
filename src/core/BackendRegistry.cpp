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

void BackendRegistry::mark_healthy(
    const std::string& host,
    const std::string& port
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host == host && backend.port == port) {
            backend.healthy = true;
            return;
        }
    }
}

void BackendRegistry::mark_unhealthy(
    const std::string& host,
    const std::string& port
) {
    std::lock_guard<std::mutex> lock(mutex_);

    for (auto& backend : backends_) {
        if (backend.host == host && backend.port == port) {
            backend.healthy = false;
            return;
        }
    }
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