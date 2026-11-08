#pragma once

#include "aether/core/BackendRegistry.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <cstddef>
#include <memory>

namespace aether::core {

class BackendHealthMonitor
    : public std::enable_shared_from_this<BackendHealthMonitor> {
public:
    BackendHealthMonitor(
        boost::asio::io_context& io_context,
        std::shared_ptr<BackendRegistry> registry,
        std::chrono::seconds interval = std::chrono::seconds(5),
        std::chrono::seconds timeout = std::chrono::seconds(2),
        std::size_t success_threshold = 2,
        std::size_t failure_threshold = 2
    );

    void start();
    void stop();

private:
    void check_backends();
    void on_check_complete();
    void schedule_next_check();

    boost::asio::io_context& io_context_;
    std::shared_ptr<BackendRegistry> registry_;
    boost::asio::steady_timer timer_;

    std::chrono::seconds interval_;
    std::chrono::seconds timeout_;

    std::size_t success_threshold_;
    std::size_t failure_threshold_;

    std::size_t pending_checks_{0};

    bool cycle_running_{false};
    bool stopped_{true};
};

} // namespace aether::core