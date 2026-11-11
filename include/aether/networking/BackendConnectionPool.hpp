
#pragma once

#include "aether/core/BackendRegistry.hpp"

#include <boost/asio.hpp>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>

namespace aether::networking
{

    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    class BackendConnectionPool
    {
    public:
        using Connection = std::shared_ptr<tcp::socket>;

        using AcquireHandler = std::function<void(
            boost::system::error_code,
            Connection)>;

        using RequestId = std::uint64_t;
        struct PoolStats
        {
            std::size_t total_connections{0};
            std::size_t idle_connections{0};
            std::size_t waiting_requests{0};
            std::size_t max_connections{0};
        };

        explicit BackendConnectionPool(
            net::io_context &io_context,
            std::size_t max_connections_per_backend = 10);

        RequestId async_acquire(
            const aether::core::Backend &backend,
            AcquireHandler handler);

        void cancel_acquire(
            const aether::core::Backend &backend,
            RequestId request_id);

        void release(
            const aether::core::Backend &backend,
            Connection socket,
            bool reusable);
        std::unordered_map<std::string, PoolStats> get_stats() const;

    private:
        struct PendingRequest
        {
            RequestId id;
            aether::core::Backend backend;
            AcquireHandler handler;
        };

        struct BackendPool
        {
            std::deque<Connection> idle_connections;
            std::deque<PendingRequest> waiting_requests;
            std::size_t total_connections{0};
        };

        std::string backend_key(
            const aether::core::Backend &backend) const;

        void create_connection(
            const aether::core::Backend &backend,
            AcquireHandler handler);

        void process_waiting_requests(
            const std::string &key);

        net::io_context &io_context_;

        const std::size_t max_connections_per_backend_;

        std::unordered_map<std::string, BackendPool> pools_;

        std::atomic<RequestId> next_request_id_{1};

        mutable std::mutex mutex_;
    };

} // namespace aether::networking
