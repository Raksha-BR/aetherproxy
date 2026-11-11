
#include "aether/networking/BackendConnectionPool.hpp"

#include <stdexcept>
#include <utility>

namespace aether::networking
{

    BackendConnectionPool::BackendConnectionPool(
        net::io_context &io_context,
        std::size_t max_connections_per_backend)
        : io_context_(io_context),
          max_connections_per_backend_(max_connections_per_backend)
    {

        if (max_connections_per_backend_ == 0)
        {
            throw std::invalid_argument(
                "Maximum connections per backend must be positive.");
        }
    }

    std::string BackendConnectionPool::backend_key(
        const aether::core::Backend &backend) const
    {
        return backend.host + ":" + backend.port;
    }

    BackendConnectionPool::RequestId BackendConnectionPool::async_acquire(
        const aether::core::Backend &backend,
        AcquireHandler handler)
    {
        const auto key = backend_key(backend);
        const auto request_id = next_request_id_.fetch_add(1);

        Connection connection;
        bool create_new = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto &pool = pools_[key];

            if (!pool.idle_connections.empty())
            {
                connection = std::move(pool.idle_connections.front());
                pool.idle_connections.pop_front();
            }
            else if (
                pool.total_connections < max_connections_per_backend_)
            {
                ++pool.total_connections;
                create_new = true;
            }
            else
            {
                pool.waiting_requests.push_back(
                    PendingRequest{
                        request_id,
                        backend,
                        std::move(handler)});

                return request_id;
            }
        }

        if (connection)
        {
            net::post(
                io_context_,
                [handler = std::move(handler),
                 connection = std::move(connection)]() mutable
                {
                    handler({}, std::move(connection));
                });
        }
        else if (create_new)
        {
            create_connection(backend, std::move(handler));
        }

        return request_id;
    }

    void BackendConnectionPool::cancel_acquire(
        const aether::core::Backend &backend,
        RequestId request_id)
    {
        const auto key = backend_key(backend);

        std::lock_guard<std::mutex> lock(mutex_);

        auto it = pools_.find(key);

        if (it == pools_.end())
        {
            return;
        }

        auto &waiting = it->second.waiting_requests;

        for (auto request = waiting.begin(); request != waiting.end(); ++request)
        {
            if (request->id == request_id)
            {
                waiting.erase(request);
                return;
            }
        }
    }

    void BackendConnectionPool::create_connection(
        const aether::core::Backend &backend,
        AcquireHandler handler)
    {
        auto socket = std::make_shared<tcp::socket>(io_context_);
        auto resolver = std::make_shared<tcp::resolver>(io_context_);

        resolver->async_resolve(
            backend.host,
            backend.port,
            [this, backend, socket, resolver,
             handler = std::move(handler)](
                boost::system::error_code error,
                tcp::resolver::results_type results) mutable
            {
                if (error)
                {
                    {
                        std::lock_guard<std::mutex> lock(mutex_);
                        auto &pool = pools_[backend_key(backend)];

                        if (pool.total_connections > 0)
                        {
                            --pool.total_connections;
                        }
                    }

                    handler(error, nullptr);
                    process_waiting_requests(backend_key(backend));
                    return;
                }

                net::async_connect(
                    *socket,
                    results,
                    [this, backend, socket,
                     handler = std::move(handler)](
                        boost::system::error_code connect_error,
                        const tcp::endpoint &) mutable
                    {
                        if (connect_error)
                        {
                            {
                                std::lock_guard<std::mutex> lock(mutex_);
                                auto &pool = pools_[backend_key(backend)];

                                if (pool.total_connections > 0)
                                {
                                    --pool.total_connections;
                                }
                            }

                            handler(connect_error, nullptr);
                            process_waiting_requests(
                                backend_key(backend));
                            return;
                        }

                        handler({}, std::move(socket));
                    });
            });
    }

    void BackendConnectionPool::release(
        const aether::core::Backend &backend,
        Connection socket,
        bool reusable)
    {
        if (!socket)
        {
            return;
        }

        const auto key = backend_key(backend);

        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto &pool = pools_[key];

            if (reusable && socket->is_open())
            {
                pool.idle_connections.push_back(std::move(socket));
            }
            else
            {
                boost::system::error_code ignored;
                socket->close(ignored);

                if (pool.total_connections > 0)
                {
                    --pool.total_connections;
                }
            }
        }

        process_waiting_requests(key);
    }

    void BackendConnectionPool::process_waiting_requests(
        const std::string &key)
    {
        PendingRequest pending;
        Connection connection;
        bool create_new = false;

        {
            std::lock_guard<std::mutex> lock(mutex_);

            auto it = pools_.find(key);

            if (it == pools_.end() ||
                it->second.waiting_requests.empty())
            {
                return;
            }

            auto &pool = it->second;

            pending = std::move(pool.waiting_requests.front());
            pool.waiting_requests.pop_front();

            if (!pool.idle_connections.empty())
            {
                connection = std::move(pool.idle_connections.front());
                pool.idle_connections.pop_front();
            }
            else if (
                pool.total_connections < max_connections_per_backend_)
            {
                ++pool.total_connections;
                create_new = true;
            }
            else
            {
                pool.waiting_requests.push_front(std::move(pending));
                return;
            }
        }

        if (connection)
        {
            net::post(
                io_context_,
                [handler = std::move(pending.handler),
                 connection = std::move(connection)]() mutable
                {
                    handler({}, std::move(connection));
                });
        }
        else if (create_new)
        {
            create_connection(
                pending.backend,
                std::move(pending.handler));
        }
    }

    std::unordered_map<std::string, BackendConnectionPool::PoolStats>
    BackendConnectionPool::get_stats() const
    {
        std::lock_guard<std::mutex> lock(mutex_);

        std::unordered_map<std::string, PoolStats> result;

        for (const auto &[key, pool] : pools_)
        {
            result.emplace(
                key,
                PoolStats{
                    pool.total_connections,
                    pool.idle_connections.size(),
                    pool.waiting_requests.size(),
                    max_connections_per_backend_});
        }

        return result;
    }

} // namespace aether::networking
