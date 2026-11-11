#include "aether/core/ProxyServer.hpp"
#include "aether/networking/BackendConnectionPool.hpp"

#include <iostream>
#include <csignal>
#include <utility>

namespace aether::core
{

    ProxyServer::ProxyServer(const config::ProxyConfig& config)
    : io_context_(1),
      signals_(io_context_, SIGINT, SIGTERM),
      metrics_(std::make_shared<ProxyMetrics>()),
          registry_(
              std::make_shared<BackendRegistry>(
                  config.backends,
                  config.load_balancing_strategy)),
          connection_pool_(
              std::make_shared<networking::BackendConnectionPool>(
                  io_context_,
                  10))
    {
        const auto endpoint =
            networking::tcp::endpoint(
                networking::net::ip::make_address(config.server_host),
                config.server_port);

        health_monitor_ =
            std::make_shared<BackendHealthMonitor>(
                io_context_,
                registry_,
                config.health_check_interval,
                config.health_check_timeout,
                config.success_threshold,
                config.failure_threshold);

        listener_ =
    std::make_shared<networking::HttpListener>(
        io_context_,
        endpoint,
        registry_,
        connection_pool_,
        config.request_timeout,
        config.max_retries,
         metrics_
    );
    }

    void ProxyServer::run()
    {
        std::cout
            << "[AetherProxy] Starting configured server...\n";

        signals_.async_wait(
            [this](
                const boost::system::error_code &error,
                int signal_number)
            {
                if (error)
                {
                    return;
                }

                std::cout
                    << "\n[AetherProxy] Received signal "
                    << signal_number
                    << ". Initiating graceful shutdown...\n";

                listener_->stop();
                health_monitor_->stop();
            });

        health_monitor_->start();
        listener_->run();

        io_context_.run();

        std::cout
            << "[AetherProxy] Shutdown complete.\n";
    }

} // namespace aether::core
