#include "aether/core/BackendHealthMonitor.hpp"

#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>

#include <functional>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace aether::core {

namespace {

namespace net = boost::asio;
namespace beast = boost::beast;
namespace http = beast::http;

using tcp = net::ip::tcp;

struct CheckOperation
    : std::enable_shared_from_this<CheckOperation> {

    CheckOperation(
        net::io_context& io,
        Backend backend,
        std::chrono::seconds timeout,
        std::function<void(bool)> completion
    )
        : backend(std::move(backend)),
          resolver(io),
          socket(io),
          timer(io),
          timeout(timeout),
          completion(std::move(completion)) {

        request.version(11);
        request.method(http::verb::get);
        request.target("/health");
        request.set(http::field::host, this->backend.host);
        request.set(http::field::user_agent, "AetherProxy-HealthMonitor");
        request.keep_alive(false);
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
                        if (self->completed) {
                            return;
                        }

                        if (connect_error) {
                            self->finish(false);
                            return;
                        }

                        self->send_health_request();
                    }
                );
            }
        );
    }

    void send_health_request() {
        auto self = shared_from_this();

        http::async_write(
            socket,
            request,
            [self](
                boost::system::error_code error,
                std::size_t
            ) {
                if (self->completed) {
                    return;
                }

                if (error) {
                    self->finish(false);
                    return;
                }

                self->read_health_response();
            }
        );
    }

    void read_health_response() {
        auto self = shared_from_this();

        http::async_read(
            socket,
            buffer,
            response,
            [self](
                boost::system::error_code error,
                std::size_t
            ) {
                if (self->completed) {
                    return;
                }

                const bool healthy =
                    !error &&
                    self->response.result() == http::status::ok;

                self->finish(healthy);
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

        if (completion) {
            completion(healthy);
        }
    }

    Backend backend;

    tcp::resolver resolver;
    tcp::socket socket;
    net::steady_timer timer;

    beast::flat_buffer buffer;

    http::request<http::empty_body> request;
    http::response<http::string_body> response;

    std::chrono::seconds timeout;
    std::function<void(bool)> completion;

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

    if (!registry_) {
        throw std::invalid_argument(
            "Backend registry cannot be null."
        );
    }

    if (interval_.count() <= 0 || timeout_.count() <= 0) {
        throw std::invalid_argument(
            "Health check interval and timeout must be positive."
        );
    }
}

void BackendHealthMonitor::start() {
    if (!stopped_) {
        return;
    }

    stopped_ = false;
    check_backends();
}

void BackendHealthMonitor::stop() {
    stopped_ = true;

    boost::system::error_code ignored;
    timer_.cancel(ignored);
}

void BackendHealthMonitor::check_backends() {
    if (stopped_ || cycle_running_) {
        return;
    }

    const auto backends = registry_->all_backends();

    if (backends.empty()) {
        schedule_next_check();
        return;
    }

    cycle_running_ = true;
    pending_checks_ = backends.size();

    auto self = shared_from_this();

    for (const auto& backend : backends) {
        auto completion = [self, backend](bool success) {
            const auto transition =
                self->registry_->record_health_check(
                    backend.host,
                    backend.port,
                    success,
                    success_threshold_,
                    failure_threshold_
                );

            if (transition.has_value()) {
                if (*transition) {
                    std::cout << "[HealthMonitor] Backend recovered: ";
                } else {
                    std::cout << "[HealthMonitor] Backend unhealthy: ";
                }

                std::cout
                    << backend.host << ":" << backend.port << '\n';
            }

            self->on_check_complete();
        };

        std::make_shared<CheckOperation>(
            io_context_,
            backend,
            timeout_,
            std::move(completion)
        )->start();
    }
}

void BackendHealthMonitor::on_check_complete() {
    if (pending_checks_ == 0) {
        return;
    }

    --pending_checks_;

    if (pending_checks_ == 0) {
        cycle_running_ = false;
        schedule_next_check();
    }
}

void BackendHealthMonitor::schedule_next_check() {
    if (stopped_ || cycle_running_) {
        return;
    }

    auto self = shared_from_this();

    timer_.expires_after(interval_);

    timer_.async_wait([self](boost::system::error_code error) {
        if (error || self->stopped_) {
            return;
        }

        self->check_backends();
    });
}

} // namespace aether::core