
#include "aether/core/BackendRegistry.hpp"

#include <gtest/gtest.h>

#include <stdexcept>
#include <vector>

using namespace aether::core;

namespace {

std::vector<Backend> make_backends()
{
    return {
        {"127.0.0.1", "9001", 3},
        {"127.0.0.1", "9002", 2},
        {"127.0.0.1", "9004", 1}
    };
}

} // namespace

TEST(BackendRegistryTest, InitializesWithConfiguredBackends)
{
    BackendRegistry registry(make_backends());

    EXPECT_EQ(registry.size(), 3);
    EXPECT_EQ(registry.healthy_count(), 3);
}

TEST(BackendRegistryTest, RejectsEmptyBackendList)
{
    EXPECT_THROW(
        BackendRegistry({}),
        std::invalid_argument
    );
}

TEST(BackendRegistryTest, RejectsZeroBackendWeight)
{
    std::vector<Backend> backends = {
        {"127.0.0.1", "9001", 0}
    };

    EXPECT_THROW(
        BackendRegistry(std::move(backends)),
        std::invalid_argument
    );
}

TEST(BackendRegistryTest, RoundRobinDistributesRequests)
{
    BackendRegistry registry(
        make_backends(),
        LoadBalancingStrategy::RoundRobin
    );

    const auto first = registry.acquire_backend();
    const auto second = registry.acquire_backend();
    const auto third = registry.acquire_backend();
    const auto fourth = registry.acquire_backend();

    ASSERT_TRUE(first);
    ASSERT_TRUE(second);
    ASSERT_TRUE(third);
    ASSERT_TRUE(fourth);

    EXPECT_EQ(first->port, "9001");
    EXPECT_EQ(second->port, "9002");
    EXPECT_EQ(third->port, "9004");
    EXPECT_EQ(fourth->port, "9001");
}

TEST(BackendRegistryTest, WeightedRoundRobinRespectsWeights)
{
    BackendRegistry registry(
        make_backends(),
        LoadBalancingStrategy::WeightedRoundRobin
    );

    int count_9001 = 0;
    int count_9002 = 0;
    int count_9004 = 0;

    for (int i = 0; i < 6; ++i) {
        const auto backend = registry.acquire_backend();

        ASSERT_TRUE(backend);

        if (backend->port == "9001") {
            ++count_9001;
        } else if (backend->port == "9002") {
            ++count_9002;
        } else if (backend->port == "9004") {
            ++count_9004;
        }
    }

    EXPECT_EQ(count_9001, 3);
    EXPECT_EQ(count_9002, 2);
    EXPECT_EQ(count_9004, 1);
}

TEST(BackendRegistryTest, RandomStrategySelectsHealthyBackend)
{
    BackendRegistry registry(
        make_backends(),
        LoadBalancingStrategy::Random
    );

    for (int i = 0; i < 100; ++i) {
        const auto backend = registry.acquire_backend();

        ASSERT_TRUE(backend);
        EXPECT_TRUE(
            backend->port == "9001" ||
            backend->port == "9002" ||
            backend->port == "9004"
        );
    }
}

TEST(BackendRegistryTest, SkipsUnhealthyBackend)
{
    BackendRegistry registry(make_backends());

    EXPECT_TRUE(registry.mark_unhealthy("127.0.0.1", "9002"));
    EXPECT_EQ(registry.healthy_count(), 2);

    for (int i = 0; i < 10; ++i) {
        const auto backend = registry.acquire_backend();

        ASSERT_TRUE(backend);
        EXPECT_NE(backend->port, "9002");
    }
}

TEST(BackendRegistryTest, ReturnsNoBackendWhenAllAreUnhealthy)
{
    BackendRegistry registry(make_backends());

    registry.mark_unhealthy("127.0.0.1", "9001");
    registry.mark_unhealthy("127.0.0.1", "9002");
    registry.mark_unhealthy("127.0.0.1", "9004");

    EXPECT_EQ(registry.healthy_count(), 0);
    EXPECT_FALSE(registry.acquire_backend().has_value());
}

TEST(BackendRegistryTest, TracksActiveConnections)
{
    BackendRegistry registry(make_backends());

    const auto backend = registry.acquire_backend();

    ASSERT_TRUE(backend);
    EXPECT_EQ(backend->active_connections, 1);

    registry.release_backend(backend->host, backend->port);

    const auto backends = registry.all_backends();

    EXPECT_EQ(backends[0].active_connections, 0);
}

TEST(BackendRegistryTest, DoesNotAllowActiveConnectionsBelowZero)
{
    BackendRegistry registry(make_backends());

    registry.release_backend("127.0.0.1", "9001");

    const auto backends = registry.all_backends();

    EXPECT_EQ(backends[0].active_connections, 0);
}

TEST(BackendRegistryTest, HealthCheckRequiresFailureThreshold)
{
    BackendRegistry registry(make_backends());

    EXPECT_FALSE(
        registry.record_health_check(
            "127.0.0.1", "9001", false, 2, 2
        ).has_value()
    );

    EXPECT_TRUE(
        registry.record_health_check(
            "127.0.0.1", "9001", false, 2, 2
        ).has_value()
    );

    EXPECT_EQ(registry.healthy_count(), 2);
}

TEST(BackendRegistryTest, HealthCheckRequiresSuccessThreshold)
{
    BackendRegistry registry(make_backends());

    registry.mark_unhealthy("127.0.0.1", "9001");

    EXPECT_FALSE(
        registry.record_health_check(
            "127.0.0.1", "9001", true, 2, 2
        ).has_value()
    );

    const auto recovered = registry.record_health_check(
        "127.0.0.1", "9001", true, 2, 2
    );

    ASSERT_TRUE(recovered.has_value());
    EXPECT_TRUE(*recovered);
    EXPECT_EQ(registry.healthy_count(), 3);
}

TEST(BackendRegistryTest, HealthCheckIgnoresUnknownBackend)
{
    BackendRegistry registry(make_backends());

    const auto result = registry.record_health_check(
        "127.0.0.1", "9999", false, 2, 2
    );

    EXPECT_FALSE(result.has_value());
}
