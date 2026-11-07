#pragma once

#include "aether/core/BackendRegistry.hpp"

#include <boost/asio.hpp>

#include <chrono>
#include <memory>

namespace aether::core {

class BackendHealthMonitor
    : public std::enable_shared_from_this<BackendHealthMonitor> {
public:
    BackendHealthMonitor(
        boost::asio::io_context& io_context,
        std::shared_ptr<BackendRegistry> registry,
        std::chrono::seconds interval = std::chrono::seconds(5),
        std::chrono::seconds timeout = std::chrono::seconds(2)
    );

    void start();
    void stop();

private:
    void check_backends();
    void schedule_next_check();

    boost::asio::io_context& io_context_;
    std::shared_ptr<BackendRegistry> registry_;
    boost::asio::steady_timer timer_;

    std::chrono::seconds interval_;
    std::chrono::seconds timeout_;
    bool stopped_{false};
};

} // namespace aether::core