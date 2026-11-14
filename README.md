# AetherProxy

AetherProxy is a C++20 asynchronous HTTP reverse proxy designed to distribute client requests across multiple backend servers. It combines configurable load balancing, active health monitoring, backend connection pooling, request timeouts, and Prometheus-compatible metrics.

The project focuses on practical systems programming concepts including asynchronous networking, connection lifecycle management, concurrency, failure handling, and observability.

## Features

### Networking and concurrency
- Asynchronous HTTP request forwarding using Boost.Asio and Boost.Beast.
- Multiple event-loop threads for concurrent request processing.
- Strand-based handler serialization for session, listener, and health-monitor operations.
- Graceful shutdown on `SIGINT` and `SIGTERM`.

### Load balancing
- Round Robin.
- Random.
- Smooth Weighted Round Robin.
- Least Connections.
- Automatic exclusion of unhealthy backends.

### Backend health monitoring
- Active asynchronous HTTP health checks using `GET /health`.
- Passive failure detection during backend communication.
- Configurable health-check interval, timeout, failure threshold, and recovery threshold.
- Automatic backend recovery after consecutive successful checks.

### Connection management
- Per-backend connection pools.
- Configurable maximum connection capacity in the pool implementation.
- Exclusive connection leases for requests.
- Reuse of eligible HTTP keep-alive connections.
- Queuing when a backend connection limit is reached.
- Cancellation of queued acquisition requests.

### Reliability
- Configurable upstream request timeout.
- Configurable retry count for eligible backend connection-acquisition failures.
- HTTP `502 Bad Gateway`, `503 Service Unavailable`, and `504 Gateway Timeout` error handling.
- Avoids blindly retrying requests after they may have reached the backend.

### Observability
- Prometheus-compatible metrics endpoint at `/metrics`.
- Request counters and request-duration aggregates.
- Active client connection tracking.
- Backend health metrics.
- Backend connection-pool metrics.
- Runtime logging.

## Tech Stack

- **Language:** C++20
- **Networking:** Boost.Asio, Boost.Beast
- **Configuration:** yaml-cpp
- **Build system:** CMake
- **Testing:** GoogleTest, Python integration tests
- **Platform:** Linux / Ubuntu / WSL2

## Architecture

```text
                         Client
                           |
                           v
                  AetherProxy Listener
                           |
                           v
                     HTTP Session
                           |
                +----------+----------+
                |                     |
                v                     v
          Backend Registry      Connection Pool
                |                     |
                v                     v
         Load Balancer          Backend Connections
                |                     |
        +-------+-------+-------------+
        |       |       |
        v       v       v
     Backend  Backend  Backend
      :9001    :9002    :9004

       Backend Health Monitor
                  |
          GET /health checks
                  |
          Backend Registry

       Proxy Metrics
                  |
               /metrics
```

The proxy uses two `io_context` execution threads. Strands serialize operations that share session, listener, and health-monitor state, while mutexes and atomic counters protect shared state where required.

## Project Structure

```text
aetherproxy/
├── config/
│   ├── aetherproxy.yaml
|
├── dev/
│   └── backend/
│       └── server.py
|
├── include/
│   └── aether/
│       ├── config/
│       │   └── ProxyConfig.hpp
│       ├── core/
│       │   ├── BackendHealthMonitor.hpp
│       │   ├── BackendRegistry.hpp
│       │   ├── ProxyMetrics.hpp
│       │   └── ProxyServer.hpp
│       └── networking/
│           ├── BackendConnectionPool.hpp
│           ├── HttpListener.hpp
│           └── HttpSession.hpp
├── src/
│   ├── config/
│   ├── core/
│   ├── networking/
│   └── main.cpp
├── tests/
│   ├── integration/
│   └── unit/
├── CMakeLists.txt
└── README.md
```

## Prerequisites

On Ubuntu / WSL2:

```bash
sudo apt update

sudo apt install -y \
    build-essential \
    cmake \
    libboost-all-dev \
    libyaml-cpp-dev \
    libgtest-dev \
    python3
```

## Build

Clone the repository:

```bash
git clone git@github.com:Raksha-BR/aetherproxy.git
cd aetherproxy
```

Configure and build:

```bash
cmake -S . -B build
cmake --build build -j2
```

## Configuration

The default configuration is stored in:

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

request:
  timeout_seconds: 5
  max_retries: 2

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

### Configuration options

| Setting | Description |
|---|---|
| `server.host` | Address on which the proxy listens |
| `server.port` | Proxy listening port |
| `load_balancing.strategy` | Backend selection strategy |
| `health_check.interval_seconds` | Interval between health-check cycles |
| `health_check.timeout_seconds` | Timeout for an individual health check |
| `health_check.failure_threshold` | Consecutive failures required for an unhealthy transition |
| `health_check.success_threshold` | Consecutive successes required for recovery |
| `request.timeout_seconds` | Upstream request timeout |
| `request.max_retries` | Maximum configured retries for eligible connection-acquisition failures |
| `backends` | Configured backend endpoints |
| `weight` | Relative backend weight for Weighted Round Robin |

### Supported load-balancing strategies

| Strategy | YAML value | Behaviour |
|---|---|---|
| Round Robin | `round_robin` | Selects healthy backends sequentially |
| Random | `random` | Selects a healthy backend randomly |
| Weighted Round Robin | `weighted_round_robin` | Distributes requests according to backend weights |
| Least Connections | `least_connections` | Selects a backend with the fewest active connections |

For example, weights of 3:2:1 produce an approximate distribution of 15:10:5 over 30 requests when all backends remain healthy and eligible.

The configuration loader validates required settings, port ranges, backend entries, health-check thresholds, and supported strategy names.

A custom configuration file can be supplied as a command-line argument:

```bash
./build/aetherproxy path/to/custom-config.yaml
```

## Running AetherProxy

Start the backend servers first.

Open three terminals from the project root.

**Terminal 1 — Backend 9001**

```bash
python3 dev/backend/server.py --port 9001
```

**Terminal 2 — Backend 9002**

```bash
python3 dev/backend/server.py --port 9002
```

**Terminal 3 — Backend 9004**

```bash
python3 dev/backend/server.py --port 9004
```

**Terminal 4 — AetherProxy**

```bash
./build/aetherproxy
```

The default proxy endpoint is:

```text
http://127.0.0.1:8080
```

Send a request:

```bash
curl -i http://127.0.0.1:8080/
```

## Connection Pooling

AetherProxy maintains backend connection pools to reduce repeated connection establishment overhead.

The current server configuration initializes the pool with a maximum of 10 connections per backend.

The pool:
- Reuses eligible HTTP keep-alive connections.
- Grants an exclusive connection lease to each request.
- Queues acquisition requests when the pool reaches capacity.
- Tracks active and idle connections.
- Releases or closes connections according to their reusability.
- Supports cancellation of queued requests.

Connection-pool limits are reflected in the Prometheus metrics.

## Health Monitoring and Failover

The proxy combines active and passive backend health detection.

**Active health checks**

The health monitor periodically sends:

```http
GET /health
```

A backend must return HTTP `200 OK` to pass the check.

With the default configuration:
- Two consecutive failed checks are required to mark a healthy backend unhealthy.
- Two consecutive successful checks are required to recover an unhealthy backend.

**Passive failure detection**

Backend communication failures can trigger immediate unhealthy-state handling.

Unhealthy backends are excluded from normal backend selection. Once active health checks confirm recovery, they become eligible again.

## Request Timeouts and Retries

The upstream request timeout covers the backend connection-acquisition and forwarding lifecycle.

The proxy returns appropriate gateway errors when an upstream operation fails or exceeds its timeout.

Retries are deliberately restricted to eligible failures that occur before a request is forwarded. A request is not blindly replayed after it may have reached a backend, avoiding unsafe duplicate execution.

## Prometheus Metrics

Metrics are available at:

```text
http://127.0.0.1:8080/metrics
```

Inspect them using:

```bash
curl -s http://127.0.0.1:8080/metrics
```

### Exported metrics

| Metric | Type | Description |
|---|---|---|
| `aetherproxy_active_connections` | Gauge | Active client connections |
| `aetherproxy_requests_total` | Counter | Total requests received |
| `aetherproxy_requests_successful_total` | Counter | Successful requests |
| `aetherproxy_requests_failed_total` | Counter | Failed requests |
| `aetherproxy_request_duration_seconds_sum` | Summary | Aggregate request duration |
| `aetherproxy_request_duration_seconds_count` | Summary | Number of recorded request durations |
| `aetherproxy_backend_healthy` | Gauge | Backend health state |
| `aetherproxy_pool_connections_total` | Gauge | Open backend connections |
| `aetherproxy_pool_connections_idle` | Gauge | Idle backend connections |
| `aetherproxy_pool_connections_active` | Gauge | Active backend connections |
| `aetherproxy_pool_requests_waiting` | Gauge | Requests waiting for a connection |
| `aetherproxy_pool_connections_max` | Gauge | Maximum connections per backend |

Backend-specific metrics include a `backend` label.

The request-duration summary currently exposes aggregate sum and count; histogram buckets and quantiles are not provided.

## Testing

Build and run the complete test suite:

```bash
cmake --build build -j2

ctest --test-dir build --output-on-failure
```

The test suite includes:
- Backend registry unit tests.
- Load-balancing and health-state tests.
- Prometheus metrics tests.
- Connection-pool tests.
- Configuration validation tests.
- HTTP forwarding integration tests.
- Round-robin integration tests.
- Health failover integration tests.

The currently verified test suite contains **33 tests**, all passing in the development environment.

## Performance Benchmarking

AetherProxy was benchmarked locally using `wrk` with three threaded Python backend servers.

Example command:

```bash
wrk -t2 -c50 -d30s --timeout 15s --latency \
    http://127.0.0.1:8080/
```

One recorded run produced:

| Metric | Result |
|---|---:|
| Duration | 30 seconds |
| Client threads | 2 |
| Concurrent connections | 50 |
| Requests per second | 728.52 |
| Average latency | 84.90 ms |
| Median latency | 60.63 ms |
| 90th percentile latency | 119.02 ms |
| 99th percentile latency | 802.77 ms |
| Total requests | 21,890 |

These are local development measurements, not production capacity guarantees. Results depend on the host machine, backend implementation, workload, and system configuration.

## Graceful Shutdown

AetherProxy handles `SIGINT` and `SIGTERM`.

On shutdown, it stops accepting new client connections, stops scheduled health checks, and allows active sessions to complete their shutdown lifecycle before closing their resources.

Stop the proxy with:

```text
Ctrl+C
```

## Development Status

Implemented and verified:
- Asynchronous HTTP reverse proxy.
- Multiple backend support.
- Four load-balancing strategies.
- Active and passive backend health monitoring.
- Backend failure exclusion and recovery.
- Per-backend connection pooling.
- HTTP keep-alive connection reuse.
- Request timeouts and restricted retries.
- Multi-threaded event-loop execution.
- Strand-based asynchronous handler serialization.
- Graceful shutdown.
- Prometheus-compatible metrics.
- YAML configuration and validation.
- Unit and integration test suite.

## Current Scope and Limitations

- The application currently implements HTTP proxying; HTTPS termination is not provided by the proxy itself.
- Performance figures are from a local development environment.
- The request-duration summary does not currently expose histogram buckets.


## License

No license has been specified yet.
