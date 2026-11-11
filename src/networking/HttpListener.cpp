#include "aether/networking/HttpListener.hpp"
#include "aether/networking/HttpSession.hpp"
#include "aether/core/BackendRegistry.hpp"

#include <iostream>
#include <utility>

namespace aether::networking
{

    HttpListener::HttpListener(
        net::io_context &io_context,
        const tcp::endpoint &endpoint,
        std::shared_ptr<aether::core::BackendRegistry> registry,
        std::shared_ptr<BackendConnectionPool> connection_pool,
        std::chrono::seconds request_timeout,
        std::size_t max_retries,
        std::shared_ptr<aether::core::ProxyMetrics> metrics)
        : io_context_(io_context),
          registry_(std::move(registry)),
          metrics_(std::move(metrics)),
          connection_pool_(std::move(connection_pool)),
          request_timeout_(request_timeout),
          max_retries_(max_retries),
          acceptor_(io_context)
    {

        beast::error_code error;

        acceptor_.open(endpoint.protocol(), error);

        if (error)
        {
            throw beast::system_error(error);
        }

        acceptor_.set_option(
            net::socket_base::reuse_address(true),
            error);

        if (error)
        {
            throw beast::system_error(error);
        }

        acceptor_.bind(endpoint, error);

        if (error)
        {
            throw beast::system_error(error);
        }

        acceptor_.listen(
            net::socket_base::max_listen_connections,
            error);

        if (error)
        {
            throw beast::system_error(error);
        }
    }

    void HttpListener::run()
    {
        accept_connection();
    }

    void HttpListener::stop()
    {
        if (stopping_)
        {
            return;
        }

        stopping_ = true;

        beast::error_code ignored;
        acceptor_.cancel(ignored);
        acceptor_.close(ignored);

        for (auto &weak_session : sessions_)
        {
            if (auto session = weak_session.lock())
            {
                session->stop();
            }
        }

        sessions_.clear();

        std::cout
            << "[AetherProxy] Listener stopped accepting connections\n";
    }

    void HttpListener::accept_connection()
    {
        if (stopping_)
        {
            return;
        }

        auto self = shared_from_this();

        acceptor_.async_accept(
            io_context_,
            [self](
                beast::error_code error,
                tcp::socket socket)
            {
                if (!error && !self->stopping_)
                {
                    std::cerr
                        << "[AetherProxy] Accepted client connection\n";

                    // Remove expired sessions before tracking new ones.
                    std::erase_if(
                        self->sessions_,
                        [](const std::weak_ptr<HttpSession> &session)
                        {
                            return session.expired();
                        });

                    auto session = std::make_shared<HttpSession>(
                        std::move(socket),
                        self->registry_,
                        self->connection_pool_,
                        self->request_timeout_,
                        self->max_retries_,
                        self->metrics_);

                    self->sessions_.push_back(session);
                    session->run();
                }
                else if (error && !self->stopping_ &&
                         error != net::error::operation_aborted)
                {
                    std::cerr
                        << "[AetherProxy] Accept error: "
                        << error.message() << '\n';
                }

                if (!self->stopping_)
                {
                    self->accept_connection();
                }
            });
    }

} // namespace aether::networking
