
#include "aether/networking/BackendConnectionPool.hpp"

#include <gtest/gtest.h>

#include <boost/asio.hpp>

#include <cstddef>
#include <memory>
#include <string>
#include <utility>

namespace
{

using namespace aether::core;
using namespace aether::networking;

namespace net = boost::asio;
using tcp = net::ip::tcp;

class BackendConnectionPoolTest : public ::testing::Test
{
protected:
    net::io_context io_context;

    tcp::acceptor acceptor{
        io_context,
        tcp::endpoint(tcp::v4(), 0)
    };

    tcp::socket server_socket{io_context};

    Backend backend{
        "127.0.0.1",
        std::to_string(acceptor.local_endpoint().port()),
        1,
        true
    };

    BackendConnectionPool pool{io_context, 1};

    BackendConnectionPool::Connection acquire_connection()
    {
        acceptor.async_accept(
            server_socket,
            [](boost::system::error_code) {});

        BackendConnectionPool::Connection connection;
        boost::system::error_code acquire_error;
        bool callback_called = false;

        pool.async_acquire(
            backend,
            [&](boost::system::error_code error,
                BackendConnectionPool::Connection acquired)
            {
                acquire_error = error;
                connection = std::move(acquired);
                callback_called = true;
            });

        io_context.run();

        EXPECT_TRUE(callback_called);
        EXPECT_FALSE(acquire_error);
        EXPECT_NE(connection, nullptr);

        return connection;
    }
};

TEST(BackendConnectionPoolConstructorTest, RejectsZeroConnectionLimit)
{
    net::io_context context;

    EXPECT_THROW(
        BackendConnectionPool(context, 0),
        std::invalid_argument);
}

TEST_F(BackendConnectionPoolTest, CreatesAndReusesBackendConnection)
{
    auto original_connection = acquire_connection();

    ASSERT_NE(original_connection, nullptr);

    auto stats = pool.get_stats();

    ASSERT_EQ(stats.size(), 1U);

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).total_connections, 1U);
    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).idle_connections, 0U);

    pool.release(backend, original_connection, true);

    stats = pool.get_stats();

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).idle_connections, 1U);

    BackendConnectionPool::Connection reused_connection;
    bool callback_called = false;

    io_context.restart();

    pool.async_acquire(
        backend,
        [&](boost::system::error_code error,
            BackendConnectionPool::Connection acquired)
        {
            EXPECT_FALSE(error);
            reused_connection = std::move(acquired);
            callback_called = true;
        });

    io_context.run();

    ASSERT_TRUE(callback_called);
    ASSERT_NE(reused_connection, nullptr);

    EXPECT_EQ(original_connection.get(), reused_connection.get());

    stats = pool.get_stats();

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).total_connections, 1U);
    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).idle_connections, 0U);
}

TEST_F(BackendConnectionPoolTest, QueuesRequestWhenConnectionLimitIsReached)
{
    auto connection = acquire_connection();

    ASSERT_NE(connection, nullptr);

    bool second_callback_called = false;

    const auto request_id = pool.async_acquire(
        backend,
        [&](boost::system::error_code,
            BackendConnectionPool::Connection)
        {
            second_callback_called = true;
        });

    EXPECT_NE(request_id, 0U);

    auto stats = pool.get_stats();

    ASSERT_EQ(stats.size(), 1U);

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).total_connections, 1U);
    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).waiting_requests, 1U);

    EXPECT_FALSE(second_callback_called);
}

TEST_F(BackendConnectionPoolTest, CancelsQueuedRequest)
{
    auto connection = acquire_connection();

    ASSERT_NE(connection, nullptr);

    bool second_callback_called = false;

    const auto request_id = pool.async_acquire(
        backend,
        [&](boost::system::error_code,
            BackendConnectionPool::Connection)
        {
            second_callback_called = true;
        });

    auto stats = pool.get_stats();

    ASSERT_EQ(stats.at("127.0.0.1:" + backend.port).waiting_requests, 1U);

    pool.cancel_acquire(backend, request_id);

    stats = pool.get_stats();

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).waiting_requests, 0U);

    pool.release(backend, connection, true);

    io_context.restart();
    io_context.run();

    EXPECT_FALSE(second_callback_called);

    stats = pool.get_stats();

    EXPECT_EQ(stats.at("127.0.0.1:" + backend.port).idle_connections, 1U);
}

}
