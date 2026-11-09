# AetherProxy

AetherProxy is a C++20 asynchronous HTTP reverse proxy that distributes incoming client requests across multiple backend servers using configurable load-balancing strategies.

The project demonstrates networking, asynchronous I/O, backend health monitoring, failure detection, automatic recovery, and YAML-based configuration management.

## Features

- HTTP reverse proxy using Boost.Asio and Boost.Beast.
- Configurable Round Robin, Random, and Weighted Round Robin load balancing.
- Smooth weighted scheduling based on backend weights.
- Passive backend failure detection.
- Active asynchronous HTTP health monitoring using `GET /health`.
- Configurable health-check intervals, timeouts, and recovery thresholds.
- YAML-based server, backend, and load-balancing configuration.
- Configurable backend weights.
- Automatic unhealthy backend exclusion and recovery.
- Upstream request and health-check timeouts.
- Runtime logging.
- Configuration validation, including invalid ports, duplicate backend entries, invalid strategies, and backend weights.

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
3. The registry selects a healthy backend using the configured load-balancing strategy.
4. The proxy forwards the request to that backend.
5. The backend response is returned to the client.

### 2. Load-Balancing Strategies

AetherProxy supports three backend selection strategies.

**Round Robin**

Distributes requests sequentially across healthy backends.

```text
Request 1 -> Backend 9001
Request 2 -> Backend 9002
Request 3 -> Backend 9004
Request 4 -> Backend 9001
```

**Random**

Selects a healthy backend randomly for each incoming request.

**Weighted Round Robin**

Uses a smooth weighted round-robin scheduling algorithm to distribute requests according to configured backend weights.

For example, weights of 3:2:1 produce approximately:

| Backend | Weight | Requests out of 30 |
|---|---:|---:|
| 9001 | 3 | 15 |
| 9002 | 2 | 10 |
| 9004 | 1 | 5 |

Unhealthy backends are excluded from backend selection.

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

load_balancing:
  strategy: round_robin

health_check:
  interval_seconds: 5
  timeout_seconds: 2
  failure_threshold: 2
  success_threshold: 2

backends:
  - host: 127.0.0.1
    port: 9001
    weight: 3

  - host: 127.0.0.1
    port: 9002
    weight: 2

  - host: 127.0.0.1
    port: 9004
    weight: 1
```

### Configuration Options

| Setting | Description |
|---|---|
| `server.host` | IP address on which the proxy listens |
| `server.port` | Proxy listening port |
| `load_balancing.strategy` | Backend selection strategy |
| `interval_seconds` | Time between health-check cycles |
| `timeout_seconds` | Health-check timeout |
| `failure_threshold` | Failed checks required to mark a backend unhealthy |
| `success_threshold` | Successful checks required to recover a backend |
| `backends` | List of backend host and port combinations |
| `weight` | Relative backend weight for Weighted Round Robin; defaults to 1 |

Supported strategy values:

| Strategy | YAML value |
|---|---|
| Round Robin | `round_robin` |
| Random | `random` |
| Weighted Round Robin | `weighted_round_robin` |

Round Robin is the default strategy when the `load_balancing` section is omitted.

The configuration loader validates required fields, port ranges, health-check thresholds, duplicate backend entries, strategy names, and backend weights.

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

## Run Test Backends

Start all three test backends from one terminal:

```bash
python3 - <<'PY'
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Thread, Event

class Handler(BaseHTTPRequestHandler):
    def do_GET(self):
        if self.path == "/health":
            body = b"OK"
        else:
            body = f"Backend {self.server.server_port} received: {self.path}\n".encode()

        self.send_response(200)
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, fmt, *args):
        print(f"[Backend {self.server.server_port}] {fmt % args}")

servers = []

for port in (9001, 9002, 9004):
    server = ThreadingHTTPServer(("127.0.0.1", port), Handler)
    Thread(target=server.serve_forever, daemon=True).start()
    servers.append(server)
    print(f"Backend listening on {port}")

Event().wait()
PY
```

Keep this terminal running while testing the proxy.

## Testing

Send a request through the proxy:

```bash
curl -i http://127.0.0.1:8080/test
```

### Test Round Robin

Set the strategy to `round_robin` in `config/aetherproxy.yaml`, restart AetherProxy, and send multiple requests:

```bash
for i in {1..9}; do
    curl -s http://127.0.0.1:8080/test
done
```

With all backends healthy, requests should be distributed equally.

### Test Random

Set the strategy to `random`, restart AetherProxy, and send multiple requests:

```bash
for i in {1..30}; do
    curl -s http://127.0.0.1:8080/test
done
```

Requests should be distributed randomly across healthy backends.

### Test Weighted Round Robin

Set the strategy to `weighted_round_robin`, restart AetherProxy, and send multiple requests:

```bash
for i in {1..30}; do
    curl -s http://127.0.0.1:8080/test
done
```

With weights 3:2:1, the expected distribution over 30 requests is approximately 15, 10, and 5 requests respectively.

After restarting AetherProxy, allow the active health monitor to complete its required successful checks before sending test traffic.

### Check Backend Health Endpoints

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
- Multiple backend support
- Round Robin load balancing
- Random load balancing
- Smooth Weighted Round Robin load balancing
- Configurable backend weights
- Passive backend failure detection
- Active asynchronous HTTP health checks
- Configurable health-check intervals and thresholds
- YAML-based server, backend, and load-balancing configuration
- Configuration validation
- Custom configuration file support
- Automatic backend exclusion and recovery
- Upstream and health-check timeouts
- Runtime logging

Planned:

- Connection pooling
- Metrics and observability
- Docker support
- Cloud deployment
- Integration and stress testing

## License
