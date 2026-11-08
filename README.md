# AetherProxy

AetherProxy is a C++20 asynchronous HTTP reverse proxy that distributes incoming client requests across multiple backend servers using round-robin load balancing.

The project demonstrates networking, asynchronous I/O, backend health monitoring, failure detection, automatic recovery, and YAML-based configuration management.

## Features

- HTTP reverse proxy using Boost.Asio and Boost.Beast.
- Round-robin load balancing across healthy backends.
- Passive backend failure detection.
- Active asynchronous HTTP health monitoring using `GET /health`.
- Configurable health-check intervals, timeouts, and recovery thresholds.
- YAML-based server and backend configuration.
- Runtime backend registration through configuration.
- Automatic unhealthy backend exclusion and recovery.
- Upstream request and health-check timeouts.
- Runtime logging.
- Configuration validation, including invalid ports and duplicate backend entries.

## Tech Stack

- C++20
- Boost.Asio
- Boost.Beast
- yaml-cpp
- CMake
- Linux / Ubuntu (WSL2)

## Architecture

```text
Client
  |
  v
AetherProxy HTTP Listener
  |
  v
HTTP Session
  |
  v
Backend Registry ---- Health Monitor
  |                    |
  |                    +-- HTTP GET /health
  |
  +---- Backend 1 :9001
  +---- Backend 2 :9002
  +---- Backend 3 :9004
```

## Project Structure

```text
aetherproxy/
├── config/
│   ├── aetherproxy.yaml
│   ├── backends.json
│   └── caddy/
│       └── Caddyfile
├── include/aether/
│   ├── config/
│   │   └── ProxyConfig.hpp
│   ├── core/
│   │   ├── BackendRegistry.hpp
│   │   ├── BackendHealthMonitor.hpp
│   │   └── ProxyServer.hpp
│   └── networking/
│       ├── HttpListener.hpp
│       └── HttpSession.hpp
├── src/
│   ├── config/
│   │   └── ProxyConfig.cpp
│   ├── core/
│   │   ├── BackendRegistry.cpp
│   │   ├── BackendHealthMonitor.cpp
│   │   └── ProxyServer.cpp
│   ├── networking/
│   │   ├── HttpListener.cpp
│   │   └── HttpSession.cpp
│   └── main.cpp
├── CMakeLists.txt
├── README.md
└── .gitignore
```

## How It Works

### 1. HTTP Request Forwarding

1. The listener accepts a client connection.
2. An HTTP session reads the incoming request.
3. The registry selects the next healthy backend.
4. The proxy forwards the request to that backend.
5. The backend response is returned to the client.

### 2. Round-Robin Load Balancing

Requests are distributed across healthy backends in sequence.

Example:

```text
Request 1 -> Backend 9001
Request 2 -> Backend 9002
Request 3 -> Backend 9004
Request 4 -> Backend 9001
```

Unhealthy backends are skipped.

### 3. Passive Health Detection

Failures during backend DNS resolution, connection, request writing, or response reading can mark a backend unhealthy immediately.

### 4. Active HTTP Health Monitoring

The health monitor periodically sends an HTTP `GET /health` request to each configured backend.

A backend is considered healthy when it responds with HTTP `200 OK`.

Default settings:

| Setting | Value |
|---|---:|
| Health-check interval | 5 seconds |
| Health-check timeout | 2 seconds |
| Consecutive failures | 2 |
| Consecutive successes | 2 |

An unhealthy backend requires two consecutive successful active checks before recovery. A healthy backend requires two consecutive failed active checks before becoming unhealthy.

Only health-state transitions are logged.

## Configuration

AetherProxy loads its configuration from:

```text
config/aetherproxy.yaml
```

Example:

```yaml
server:
  host: 127.0.0.1
  port: 8080

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends:
  - host: 127.0.0.1
    port: 9001

  - host: 127.0.0.1
    port: 9002

  - host: 127.0.0.1
    port: 9004
```

Configuration options:

| Setting | Description |
|---|---|
| `server.host` | IP address on which the proxy listens |
| `server.port` | Proxy listening port |
| `interval_seconds` | Time between health-check cycles |
| `timeout_seconds` | Health-check timeout |
| `failure_threshold` | Failed checks required to mark a backend unhealthy |
| `success_threshold` | Successful checks required to recover a backend |
| `backends` | List of backend host and port combinations |

The configuration loader validates required fields, port ranges, health-check thresholds, and duplicate backend entries.

You can also supply a different configuration file:

```bash
./build/aetherproxy path/to/custom-config.yaml
```

## Prerequisites

On Ubuntu / WSL2:

```bash
sudo apt update
sudo apt install build-essential cmake libboost-all-dev libyaml-cpp-dev python3
```

## Build

```bash
git clone git@github.com:Raksha-BR/aetherproxy.git
cd aetherproxy

cmake -S . -B build
cmake --build build -j2
```

## Run AetherProxy

From the project root:

```bash
./build/aetherproxy
```

The default configuration listens on:

```text
127.0.0.1:8080
```

Configured backends:

```text
127.0.0.1:9001
127.0.0.1:9002
127.0.0.1:9004
```

## Run Test Backends

Open three separate terminals. Run the following script in each terminal, changing `9001` to `9002` and `9004` for the other backends.

```bash
PORT=9001 python3 - <<'PY'
import os
from http.server import BaseHTTPRequestHandler, HTTPServer

port = int(os.environ["PORT"])

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            body = b"OK"
        else:
            body = f"Backend {port} received: {self.path}\n".encode()

        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

print(f"Backend listening on {port}")
HTTPServer(("127.0.0.1", port), Handler).serve_forever()
PY
```

## Testing

Send a request through the proxy:

```bash
curl -i http://127.0.0.1:8080/test
```

Test round-robin distribution:

```bash
for i in {1..6}; do
    curl -s http://127.0.0.1:8080/test
done
```

Check backend health endpoints:

```bash
curl -i http://127.0.0.1:9001/health
curl -i http://127.0.0.1:9002/health
curl -i http://127.0.0.1:9004/health
```

Each healthy backend should return HTTP `200 OK`.

## Test Failure Detection and Recovery

1. Start all configured backends and AetherProxy.
2. Configure one backend to return HTTP `503` for `/health`.
3. Wait for the configured consecutive failure threshold.
4. Verify the unhealthy transition in the AetherProxy terminal.
5. Restore the backend so `/health` returns HTTP `200`.
6. Wait for the configured consecutive success threshold.
7. Verify the recovery transition.

## Logging

Display logs and save them to a file:

```bash
mkdir -p logs
./build/aetherproxy 2>&1 | tee -a logs/aetherproxy.log
```

Inspect saved logs:

```bash
tail -f logs/aetherproxy.log
```

## Development Status

Implemented:

- HTTP request forwarding
- Round-robin load balancing
- Multiple backend support
- Passive backend failure detection
- Active asynchronous HTTP health checks
- Configurable health-check intervals and thresholds
- YAML-based server and backend configuration
- Configuration validation
- Custom configuration file support
- Automatic backend exclusion and recovery
- Upstream and health-check timeouts
- Runtime logging

Planned:

- Configurable load-balancing strategies
- Connection pooling
- Metrics and observability
- Docker support
- Cloud deployment
- Integration and stress testing

## License

MIT License.