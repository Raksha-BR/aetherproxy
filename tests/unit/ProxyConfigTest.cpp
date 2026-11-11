
#include "aether/config/ProxyConfig.hpp"

#include <gtest/gtest.h>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>

namespace
{

using namespace aether::config;
using namespace aether::core;
using namespace std::chrono_literals;

class ProxyConfigTest : public ::testing::Test
{
protected:
    std::filesystem::path config_path =
        std::filesystem::temp_directory_path() /
        "aetherproxy_unit_test_config.yaml";

    void write_config(const std::string& content)
    {
        std::ofstream file(config_path);

        if (!file)
        {
            throw std::runtime_error("Could not create test config file.");
        }

        file << content;
    }

    ProxyConfig load_config()
    {
        return ProxyConfig::load_from_file(config_path.string());
    }

    void TearDown() override
    {
        std::error_code error;
        std::filesystem::remove(config_path, error);
    }

    const std::string valid_config = R"(
server:
  host: 127.0.0.1
  port: 8080

load_balancing:
  strategy: least_connections

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

request:
  timeout_seconds: 10
  max_retries: 3

backends:
  - host: 127.0.0.1
    port: 9001
    weight: 3
  - host: 127.0.0.1
    port: 9002
    weight: 2
)";
};

TEST_F(ProxyConfigTest, LoadsValidConfiguration)
{
    write_config(valid_config);

    const auto config = load_config();

    EXPECT_EQ(config.server_host, "127.0.0.1");
    EXPECT_EQ(config.server_port, 8080);

    EXPECT_EQ(
        config.load_balancing_strategy,
        LoadBalancingStrategy::LeastConnections);

    EXPECT_EQ(config.health_check_interval, 5s);
    EXPECT_EQ(config.health_check_timeout, 2s);

    EXPECT_EQ(config.failure_threshold, 2U);
    EXPECT_EQ(config.success_threshold, 2U);

    EXPECT_EQ(config.request_timeout, 10s);
    EXPECT_EQ(config.max_retries, 3U);

    ASSERT_EQ(config.backends.size(), 2U);

    EXPECT_EQ(config.backends[0].host, "127.0.0.1");
    EXPECT_EQ(config.backends[0].port, "9001");
    EXPECT_EQ(config.backends[0].weight, 3U);

    EXPECT_EQ(config.backends[1].port, "9002");
    EXPECT_EQ(config.backends[1].weight, 2U);
}

TEST_F(ProxyConfigTest, UsesDefaultRequestSettings)
{
    write_config(R"(
server:
  host: 127.0.0.1
  port: 8080

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends:
  - host: 127.0.0.1
    port: 9001
)");

    const auto config = load_config();

    EXPECT_EQ(config.request_timeout, 5s);
    EXPECT_EQ(config.max_retries, 2U);

    EXPECT_EQ(
        config.load_balancing_strategy,
        LoadBalancingStrategy::RoundRobin);

    EXPECT_EQ(config.backends[0].weight, 1U);
}

TEST_F(ProxyConfigTest, RejectsMissingRequiredSections)
{
    write_config(R"(
server:
  host: 127.0.0.1
  port: 8080

backends:
  - host: 127.0.0.1
    port: 9001
)");

    EXPECT_THROW(load_config(), std::runtime_error);
}

TEST_F(ProxyConfigTest, RejectsInvalidServerPort)
{
    write_config(R"(
server:
  host: 127.0.0.1
  port: 0

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends:
  - host: 127.0.0.1
    port: 9001
)");

    EXPECT_THROW(load_config(), std::runtime_error);
}

TEST_F(ProxyConfigTest, RejectsDuplicateBackendEndpoints)
{
    write_config(R"(
server:
  host: 127.0.0.1
  port: 8080

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends:
  - host: 127.0.0.1
    port: 9001
  - host: 127.0.0.1
    port: 9001
)");

    EXPECT_THROW(load_config(), std::runtime_error);
}

TEST_F(ProxyConfigTest, RejectsEmptyBackendList)
{
    write_config(R"(
server:
  host: 127.0.0.1
  port: 8080

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends: []
)");

    EXPECT_THROW(load_config(), std::runtime_error);
}

TEST_F(ProxyConfigTest, RejectsUnsupportedLoadBalancingStrategy)
{
    std::string config = valid_config;

    const auto position = config.find("least_connections");

    ASSERT_NE(position, std::string::npos);

    config.replace(
        position,
        std::string("least_connections").length(),
        "unsupported_strategy");

    write_config(config);

    EXPECT_THROW(load_config(), std::runtime_error);
}

}
