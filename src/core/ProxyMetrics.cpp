#include "aether/core/ProxyMetrics.hpp"
#include "aether/core/BackendRegistry.hpp"

#include <sstream>

namespace aether::core
{

    void ProxyMetrics::connection_opened()
    {
        active_connections_.fetch_add(1, std::memory_order_relaxed);
    }

    void ProxyMetrics::connection_closed()
    {
        active_connections_.fetch_sub(1, std::memory_order_relaxed);
    }

    void ProxyMetrics::request_received()
    {
        requests_total_.fetch_add(1, std::memory_order_relaxed);
    }

    void ProxyMetrics::request_succeeded(
        std::chrono::steady_clock::duration duration)
    {
        requests_successful_.fetch_add(1, std::memory_order_relaxed);
        request_duration_count_.fetch_add(1, std::memory_order_relaxed);

        const auto nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                duration)
                .count();

        request_duration_nanoseconds_.fetch_add(
            static_cast<std::uint64_t>(nanoseconds),
            std::memory_order_relaxed);
    }

    void ProxyMetrics::request_failed(
        std::chrono::steady_clock::duration duration)
    {
        requests_failed_.fetch_add(1, std::memory_order_relaxed);
        request_duration_count_.fetch_add(1, std::memory_order_relaxed);

        const auto nanoseconds =
            std::chrono::duration_cast<std::chrono::nanoseconds>(
                duration)
                .count();

        request_duration_nanoseconds_.fetch_add(
            static_cast<std::uint64_t>(nanoseconds),
            std::memory_order_relaxed);
    }

std::string ProxyMetrics::render(
    const BackendRegistry& registry,
    const aether::networking::BackendConnectionPool& connection_pool
) const    {
        const auto active =
            active_connections_.load(std::memory_order_relaxed);

        const auto total =
            requests_total_.load(std::memory_order_relaxed);

        const auto successful =
            requests_successful_.load(std::memory_order_relaxed);

        const auto failed =
            requests_failed_.load(std::memory_order_relaxed);

        const auto duration_count =
            request_duration_count_.load(std::memory_order_relaxed);

        const auto duration_nanoseconds =
            request_duration_nanoseconds_.load(std::memory_order_relaxed);

        const double duration_seconds =
            static_cast<double>(duration_nanoseconds) / 1'000'000'000.0;

        std::ostringstream output;

        output << "# HELP aetherproxy_active_connections Current active client connections\n";
        output << "# TYPE aetherproxy_active_connections gauge\n";
        output << "aetherproxy_active_connections " << active << '\n';

        output << "# HELP aetherproxy_requests_total Total HTTP requests received\n";
        output << "# TYPE aetherproxy_requests_total counter\n";
        output << "aetherproxy_requests_total " << total << '\n';

        output << "# HELP aetherproxy_requests_successful_total Successful HTTP requests\n";
        output << "# TYPE aetherproxy_requests_successful_total counter\n";
        output << "aetherproxy_requests_successful_total " << successful << '\n';

        output << "# HELP aetherproxy_requests_failed_total Failed HTTP requests\n";
        output << "# TYPE aetherproxy_requests_failed_total counter\n";
        output << "aetherproxy_requests_failed_total " << failed << '\n';

        output << "# HELP aetherproxy_request_duration_seconds HTTP request duration\n";
        output << "# TYPE aetherproxy_request_duration_seconds summary\n";
        output << "aetherproxy_request_duration_seconds_sum "
               << duration_seconds << '\n';
        output << "aetherproxy_request_duration_seconds_count "
               << duration_count << '\n';

        output << "# HELP aetherproxy_backend_healthy Backend health status (1 = healthy, 0 = unhealthy)\n";
        output << "# TYPE aetherproxy_backend_healthy gauge\n";

        for (const auto &backend : registry.all_backends())
        {
            output << "aetherproxy_backend_healthy{backend=\""
                   << backend.host << ":" << backend.port
                   << "\"} "
                   << (backend.healthy ? 1 : 0)
                   << '\n';
        }
            const auto pool_stats = connection_pool.get_stats();

    output << "# HELP aetherproxy_pool_connections_total Total open backend connections\n";
    output << "# TYPE aetherproxy_pool_connections_total gauge\n";

    output << "# HELP aetherproxy_pool_connections_idle Idle backend connections\n";
    output << "# TYPE aetherproxy_pool_connections_idle gauge\n";

    output << "# HELP aetherproxy_pool_connections_active Active backend connections\n";
    output << "# TYPE aetherproxy_pool_connections_active gauge\n";

    output << "# HELP aetherproxy_pool_requests_waiting Requests waiting for a backend connection\n";
    output << "# TYPE aetherproxy_pool_requests_waiting gauge\n";

    output << "# HELP aetherproxy_pool_connections_max Maximum connections per backend\n";
    output << "# TYPE aetherproxy_pool_connections_max gauge\n";

    for (const auto& [backend, stats] : pool_stats)
    {
        output << "aetherproxy_pool_connections_total{backend=\""
               << backend << "\"} "
               << stats.total_connections << '\n';

        output << "aetherproxy_pool_connections_idle{backend=\""
               << backend << "\"} "
               << stats.idle_connections << '\n';

        output << "aetherproxy_pool_connections_active{backend=\""
               << backend << "\"} "
               << (stats.total_connections - stats.idle_connections) << '\n';

        output << "aetherproxy_pool_requests_waiting{backend=\""
               << backend << "\"} "
               << stats.waiting_requests << '\n';

        output << "aetherproxy_pool_connections_max{backend=\""
               << backend << "\"} "
               << stats.max_connections << '\n';
    }
        return output.str();
    }

} // namespace aether::core