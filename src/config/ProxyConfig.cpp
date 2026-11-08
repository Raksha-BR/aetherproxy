#include "aether/config/ProxyConfig.hpp"

#include <yaml-cpp/yaml.h>

#include <set>
#include <stdexcept>
#include <string>

namespace aether::config {

namespace {

std::string required_string(
    const YAML::Node& node,
    const std::string& key
) {
    if (!node[key] || !node[key].IsScalar()) {
        throw std::runtime_error(
            "Missing or invalid configuration field: " + key
        );
    }

    const auto value = node[key].as<std::string>();

    if (value.empty()) {
        throw std::runtime_error(
            "Configuration field cannot be empty: " + key
        );
    }

    return value;
}

unsigned int required_port(
    const YAML::Node& node,
    const std::string& key
) {
    if (!node[key] || !node[key].IsScalar()) {
        throw std::runtime_error(
            "Missing or invalid port field: " + key
        );
    }

    const auto port = node[key].as<unsigned int>();

    if (port == 0 || port > 65535) {
        throw std::runtime_error(
            "Port must be between 1 and 65535: " + key
        );
    }

    return port;
}

} // namespace

ProxyConfig ProxyConfig::load_from_file(const std::string& path) {
    const YAML::Node root = YAML::LoadFile(path);

    if (!root.IsMap()) {
        throw std::runtime_error(
            "Configuration root must be a YAML mapping."
        );
    }

    const auto server = root["server"];
    const auto health = root["health_check"];
    const auto backends_node = root["backends"];

    if (!server || !server.IsMap() ||
        !health || !health.IsMap() ||
        !backends_node || !backends_node.IsSequence()) {
        throw std::runtime_error(
            "Invalid configuration structure."
        );
    }

    ProxyConfig config{};

    config.server_host =
        required_string(server, "host");

    config.server_port =
        static_cast<unsigned short>(
            required_port(server, "port")
        );

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
        config.success_threshold == 0) {
        throw std::runtime_error(
            "Health-check settings and thresholds must be positive."
        );
    }

    config.health_check_interval =
        std::chrono::seconds(interval);

    config.health_check_timeout =
        std::chrono::seconds(timeout);

    std::set<std::string> backend_endpoints;

    for (const auto& node : backends_node) {
        if (!node.IsMap()) {
            throw std::runtime_error(
                "Each backend must be a YAML mapping."
            );
        }

        const std::string host =
            required_string(node, "host");

        const auto port =
            required_port(node, "port");

        const std::string endpoint =
            host + ":" + std::to_string(port);

        if (!backend_endpoints.insert(endpoint).second) {
            throw std::runtime_error(
                "Duplicate backend configured: " + endpoint
            );
        }

        config.backends.push_back({
            host,
            std::to_string(port)
        });
    }

    if (config.backends.empty()) {
        throw std::runtime_error(
            "At least one backend must be configured."
        );
    }

    return config;
}

} // namespace aether::config