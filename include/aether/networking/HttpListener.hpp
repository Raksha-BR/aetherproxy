#pragma once

#include "aether/core/BackendRegistry.hpp"
#include "aether/networking/BackendConnectionPool.hpp"
#include "aether/networking/HttpSession.hpp"
#include "aether/core/ProxyMetrics.hpp"

#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <chrono>
#include <cstddef>
#include <algorithm>
#include <vector>

#include <memory>

namespace aether::networking {

namespace beast = boost::beast;
namespace net = boost::asio;

using tcp = net::ip::tcp;

class HttpListener
    : public std::enable_shared_from_this<HttpListener> {

public:
HttpListener(
    net::io_context& io_context,
    const tcp::endpoint& endpoint,
    std::shared_ptr<aether::core::BackendRegistry> registry,
    std::shared_ptr<BackendConnectionPool> connection_pool,
    std::chrono::seconds request_timeout,
    std::size_t max_retries,
    std::shared_ptr<aether::core::ProxyMetrics> metrics
);
    void run();

    void stop();

private:
    void accept_connection();

    // Serializes listener accept, session tracking, and shutdown handlers.
    net::strand<net::any_io_executor> strand_;

    net::io_context& io_context_;

std::shared_ptr<aether::core::BackendRegistry> registry_;
std::shared_ptr<aether::core::ProxyMetrics> metrics_;
std::shared_ptr<BackendConnectionPool> connection_pool_;
std::chrono::seconds request_timeout_;
std::size_t max_retries_;
tcp::acceptor acceptor_;
std::vector<std::weak_ptr<HttpSession>> sessions_;
bool stopping_{false};
    };

} // namespace aether::networking
