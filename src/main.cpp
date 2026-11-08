#include "aether/config/ProxyConfig.hpp"
#include "aether/core/ProxyServer.hpp"

#include <exception>
#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    try {
        const std::string config_path =
            (argc > 1) ? argv[1] : "config/aetherproxy.yaml";

        std::cout
            << "[AetherProxy] Loading configuration from "
            << config_path << '\n';

        const auto config =
            aether::config::ProxyConfig::load_from_file(config_path);

        std::cout
            << "[AetherProxy] Starting C++20 HTTP server...\n";

        aether::core::ProxyServer server(config);
        server.run();

    } catch (const std::exception& error) {
        std::cerr
            << "[AetherProxy] Fatal error: "
            << error.what() << '\n';

        return 1;
    }

    return 0;
}