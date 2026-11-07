#include "aether/core/BackendHealthMonitor.hpp"

#include <iostream>
#include <utility>

namespace aether::core {

namespace {

using tcp = boost::asio::ip::tcp;
namespace net = boost::asio;

struct CheckOperation
    : std::enable_shared_from_this<CheckOperation> {

    CheckOperation(
        net::io_context& io,
        std::shared_ptr<BackendRegistry> registry,
        Backend backend,
        std::chrono::seconds timeout
    )
        : registry(std::move(registry)),
          backend(std::move(backend)),
          resolver(io),
          socket(io),
          timer(io),
          timeout(timeout) {
    }

    void start() {
        auto self = shared_from_this();

        timer.expires_after(timeout);

        timer.async_wait([self](boost::system::error_code error) {
            if (!error) {
                self->finish(false);
            }
        });

        resolver.async_resolve(
            backend.host,
            backend.port,
            [self](
                boost::system::error_code error,
                tcp::resolver::results_type results
            ) {
                if (self->completed) {
                    return;
                }

                if (error) {
                    self->finish(false);
                    return;
                }

                net::async_connect(
                    self->socket,
                    results,
                    [self](
                        boost::system::error_code connect_error,
                        const tcp::endpoint&
                    ) {
                        self->finish(!connect_error);
                    }
                );
            }
        );
    }

    void finish(bool healthy) {
        if (completed) {
            return;
        }

        completed = true;

        boost::system::error_code ignored;

        timer.cancel(ignored);
        resolver.cancel();
        socket.close(ignored);

        if (healthy) {
            registry->mark_healthy(backend.host, backend.port);
            std::cout << "[HealthMonitor] Backend recovered: "
                      << backend.host << ":" << backend.port << '\n';
        } else {
            registry->mark_unhealthy(backend.host, backend.port);
            std::cout << "[HealthMonitor] Backend unhealthy: "
                      << backend.host << ":" << backend.port << '\n';
        }
    }

    std::shared_ptr<BackendRegistry> registry;
    Backend backend;

    tcp::resolver resolver;
    tcp::socket socket;
    net::steady_timer timer;

    std::chrono::seconds timeout;
    bool completed{false};
};

} // namespace

BackendHealthMonitor::BackendHealthMonitor(
    net::io_context& io_context,
    std::shared_ptr<BackendRegistry> registry,
    std::chrono::seconds interval,
    std::chrono::seconds timeout
)
    : io_context_(io_context),
      registry_(std::move(registry)),
      timer_(io_context),
      interval_(interval),
      timeout_(timeout) {
}

void BackendHealthMonitor::start() {
    stopped_ = false;
    check_backends();
    schedule_next_check();
}

void BackendHealthMonitor::stop() {
    stopped_ = true;

    boost::system::error_code ignored;
    timer_.cancel(ignored);
}

void BackendHealthMonitor::check_backends() {
    for (const auto& backend : registry_->all_backends()) {
        std::make_shared<CheckOperation>(
            io_context_,
            registry_,
            backend,
            timeout_
        )->start();
    }
}

void BackendHealthMonitor::schedule_next_check() {
    if (stopped_) {
        return;
    }

    auto self = shared_from_this();

    timer_.expires_after(interval_);

    timer_.async_wait([self](boost::system::error_code error) {
        if (error || self->stopped_) {
            return;
        }

        self->check_backends();
        self->schedule_next_check();
    });
}

} // namespace aether::core