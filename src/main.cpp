#include "aether/core/ProxyServer.hpp"

#include <exception>
#include <iostream>

int main() {
    try {
        std::cout
            << "[AetherProxy] Starting C++20 HTTP server...\n";

        aether::core::ProxyServer server(
            "127.0.0.1",
            8080
        );

        server.run();

    } catch (const std::exception& error) {

        std::cerr
            << "[AetherProxy] Fatal error: "
            << error.what() << '\n';

        return 1;
    }

    return 0;
}