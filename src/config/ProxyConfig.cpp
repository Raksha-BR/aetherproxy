
#include "aether/config/ProxyConfig.hpp"

#include <yaml-cpp/yaml.h>

#include <set>
#include <stdexcept>
#include <string>

namespace aether::config
{

    namespace
    {

        std::string required_string(
            const YAML::Node &node,
            const std::string &key)
        {
            if (!node[key] || !node[key].IsScalar())
            {
                throw std::runtime_error(
                    "Missing or invalid configuration field: " + key);
            }

            const auto value = node[key].as<std::string>();

            if (value.empty())
            {
                throw std::runtime_error(
                    "Configuration field cannot be empty: " + key);
            }

            return value;
        }

        unsigned int required_port(
            const YAML::Node &node,
            const std::string &key)
        {
            if (!node[key] || !node[key].IsScalar())
            {
                throw std::runtime_error(
                    "Missing or invalid port field: " + key);
            }

            const auto port = node[key].as<unsigned int>();

            if (port == 0 || port > 65535)
            {
                throw std::runtime_error(
                    "Port must be between 1 and 65535: " + key);
            }

            return port;
        }

        core::LoadBalancingStrategy parse_strategy(
            const YAML::Node &root)
        {
            const auto load_balancing = root["load_balancing"];

            if (!load_balancing)
            {
                return core::LoadBalancingStrategy::RoundRobin;
            }

            if (!load_balancing.IsMap())
            {
                throw std::runtime_error(
                    "load_balancing must be a YAML mapping.");
            }

            const std::string strategy =
                required_string(load_balancing, "strategy");

            if (strategy == "round_robin")
            {
                return core::LoadBalancingStrategy::RoundRobin;
            }

            if (strategy == "random")
            {
                return core::LoadBalancingStrategy::Random;
            }

            if (strategy == "weighted_round_robin")
            {
                return core::LoadBalancingStrategy::WeightedRoundRobin;
            }
            if (strategy == "least_connections")
            {
                return core::LoadBalancingStrategy::LeastConnections;
            }

            throw std::runtime_error(
                "Unsupported load-balancing strategy: " + strategy);
        }

        std::size_t parse_weight(const YAML::Node &node)
        {
            if (!node["weight"])
            {
                return 1;
            }

            if (!node["weight"].IsScalar())
            {
                throw std::runtime_error(
                    "Backend weight must be a positive integer.");
            }

            const auto weight = node["weight"].as<std::size_t>();

            if (weight == 0)
            {
                throw std::runtime_error(
                    "Backend weight must be greater than zero.");
            }

            return weight;
        }

    } // namespace

    ProxyConfig ProxyConfig::load_from_file(const std::string &path)
    {
        const YAML::Node root = YAML::LoadFile(path);

        if (!root.IsMap())
        {
            throw std::runtime_error(
                "Configuration root must be a YAML mapping.");
        }

        const auto server = root["server"];
        const auto health = root["health_check"];
        const auto request = root["request"];
        const auto backends_node = root["backends"];

        if (!server || !server.IsMap() ||
            !health || !health.IsMap() ||
            !backends_node || !backends_node.IsSequence())
        {
            throw std::runtime_error(
                "Invalid configuration structure.");
        }
        if (request && !request.IsMap())
        {
            throw std::runtime_error(
                "request must be a YAML mapping.");
        }

        ProxyConfig config{};

        config.server_host =
            required_string(server, "host");

        config.server_port =
            static_cast<unsigned short>(
                required_port(server, "port"));

        config.load_balancing_strategy =
            parse_strategy(root);

        const auto interval =
            health["interval_seconds"].as<unsigned int>();

        const auto timeout =
            health["timeout_seconds"].as<unsigned int>();

        config.failure_threshold =
            health["failure_threshold"].as<std::size_t>();

        config.success_threshold =
            health["success_threshold"].as<std::size_t>();

        if (interval == 0 || timeout == 0 ||
            config.failure_threshold == 0 ||
            config.success_threshold == 0)
        {
            throw std::runtime_error(
                "Health-check settings and thresholds must be positive.");
        }

        config.health_check_interval =
            std::chrono::seconds(interval);

        config.health_check_timeout =
            std::chrono::seconds(timeout);
                if (request && request["timeout_seconds"]) {
        if (!request["timeout_seconds"].IsScalar()) {
            throw std::runtime_error(
                "Request timeout must be a positive integer."
            );
        }

        const auto request_timeout =
            request["timeout_seconds"].as<unsigned int>();

        if (request_timeout == 0) {
            throw std::runtime_error(
                "Request timeout must be greater than zero."
            );
        }

        config.request_timeout =
            std::chrono::seconds(request_timeout);
    }

    if (request && request["max_retries"]) {
        if (!request["max_retries"].IsScalar()) {
            throw std::runtime_error(
                "Maximum retries must be a non-negative integer."
            );
        }

        config.max_retries =
            request["max_retries"].as<std::size_t>();
    }

        std::set<std::string> backend_endpoints;

        for (const auto &node : backends_node)
        {
            if (!node.IsMap())
            {
                throw std::runtime_error(
                    "Each backend must be a YAML mapping.");
            }

            const std::string host =
                required_string(node, "host");

            const auto port =
                required_port(node, "port");

            const auto weight = parse_weight(node);

            const std::string endpoint =
                host + ":" + std::to_string(port);

            if (!backend_endpoints.insert(endpoint).second)
            {
                throw std::runtime_error(
                    "Duplicate backend configured: " + endpoint);
            }

            config.backends.push_back({host,
                                       std::to_string(port),
                                       weight});
        }

        if (config.backends.empty())
        {
            throw std::runtime_error(
                "At least one backend must be configured.");
        }

        return config;
    }

} // namespace aether::config
