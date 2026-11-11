
#include "aether/core/ProxyMetrics.hpp"
#include "aether/core/BackendRegistry.hpp"
#include "aether/networking/BackendConnectionPool.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <chrono>
#include <string>
#include <vector>

namespace
{

using namespace aether::core;
using namespace aether::networking;
using namespace std::chrono_literals;

class ProxyMetricsTest : public ::testing::Test
{
protected:
    boost::asio::io_context io_context;

    BackendRegistry registry{
        std::vector<Backend>{
            Backend{"127.0.0.1", "9001", 1, true},
            Backend{"127.0.0.1", "9002", 1, true}
        }
    };

    BackendConnectionPool connection_pool{io_context};

    ProxyMetrics metrics;

    std::string render_metrics() const
    {
        return metrics.render(registry, connection_pool);
    }
};

TEST_F(ProxyMetricsTest, RendersInitialMetricValues)
{
    const std::string output = render_metrics();

    EXPECT_NE(output.find("aetherproxy_active_connections 0"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_requests_total 0"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_requests_successful_total 0"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_requests_failed_total 0"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_request_duration_seconds_count 0"),
              std::string::npos);
}

TEST_F(ProxyMetricsTest, TracksActiveConnections)
{
    metrics.connection_opened();
    metrics.connection_opened();

    std::string output = render_metrics();

    EXPECT_NE(output.find("aetherproxy_active_connections 2"),
              std::string::npos);

    metrics.connection_closed();

    output = render_metrics();

    EXPECT_NE(output.find("aetherproxy_active_connections 1"),
              std::string::npos);
}

TEST_F(ProxyMetricsTest, TracksRequestCounters)
{
    metrics.request_received();
    metrics.request_received();
    metrics.request_received();

    metrics.request_succeeded(100ms);
    metrics.request_succeeded(200ms);
    metrics.request_failed(300ms);

    const std::string output = render_metrics();

    EXPECT_NE(output.find("aetherproxy_requests_total 3"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_requests_successful_total 2"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_requests_failed_total 1"),
              std::string::npos);

    EXPECT_NE(output.find("aetherproxy_request_duration_seconds_count 3"),
              std::string::npos);
}

TEST_F(ProxyMetricsTest, AggregatesRequestDuration)
{
    metrics.request_succeeded(1s);
    metrics.request_failed(500ms);

    const std::string output = render_metrics();

    const auto position =
        output.find("aetherproxy_request_duration_seconds_sum ");

    ASSERT_NE(position, std::string::npos);

    const auto value_start =
        position + std::string(
            "aetherproxy_request_duration_seconds_sum ").length();

    const auto value_end = output.find('\n', value_start);

    const double duration = std::stod(
        output.substr(value_start, value_end - value_start));

    EXPECT_NEAR(duration, 1.5, 0.000001);
}

TEST_F(ProxyMetricsTest, RendersBackendHealthMetrics)
{
    const std::string output = render_metrics();

    EXPECT_NE(
        output.find(
            "aetherproxy_backend_healthy{backend=\"127.0.0.1:9001\"} 1"),
        std::string::npos);

    EXPECT_NE(
        output.find(
            "aetherproxy_backend_healthy{backend=\"127.0.0.1:9002\"} 1"),
        std::string::npos);
}

TEST_F(ProxyMetricsTest, RendersPoolMetricDefinitions)
{
    const std::string output = render_metrics();

    EXPECT_NE(output.find("# TYPE aetherproxy_pool_connections_total gauge"),
              std::string::npos);

    EXPECT_NE(output.find("# TYPE aetherproxy_pool_connections_idle gauge"),
              std::string::npos);

    EXPECT_NE(output.find("# TYPE aetherproxy_pool_connections_active gauge"),
              std::string::npos);

    EXPECT_NE(output.find("# TYPE aetherproxy_pool_requests_waiting gauge"),
              std::string::npos);

    EXPECT_NE(output.find("# TYPE aetherproxy_pool_connections_max gauge"),
              std::string::npos);
}

}

