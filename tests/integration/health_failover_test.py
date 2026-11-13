
import os
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from collections import Counter
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import socket


class BackendHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        body = (
            b"healthy\n"
            if self.path == "/health"
            else f"Backend {self.server.server_port}\n".encode()
        )

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


class ReusableHTTPServer(ThreadingHTTPServer):
    allow_reuse_address = True


def start_backend(port=0):
    server = ReusableHTTPServer(
        ("127.0.0.1", port),
        BackendHandler,
    )

    thread = threading.Thread(
        target=server.serve_forever,
        daemon=True,
    )
    thread.start()

    return server, thread


def stop_backend(server, thread):
    if server is not None:
        server.shutdown()
        server.server_close()

    if thread is not None:
        thread.join(timeout=5)


def http_get(url, timeout=2):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.status, response.read().decode().strip()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode().strip()


def wait_for_backend_state(
    proxy_port,
    backend_port,
    expected_healthy,
    process,
    timeout=15,
):
    url = f"http://127.0.0.1:{proxy_port}/metrics"
    metric = (
        f'aetherproxy_backend_healthy{{backend="127.0.0.1:{backend_port}"}} '
        f"{1 if expected_healthy else 0}"
    )

    deadline = time.monotonic() + timeout

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"Proxy exited with code {process.returncode}"
            )

        try:
            status, metrics = http_get(url)

            if status == 200 and metric in metrics:
                return

        except OSError:
            pass

        time.sleep(0.2)

    state = "healthy" if expected_healthy else "unhealthy"
    raise TimeoutError(
        f"Backend {backend_port} did not become {state}"
    )


def main():
    if len(sys.argv) != 2:
        print(
            "Usage: health_failover_test.py <path-to-aetherproxy>",
            file=sys.stderr,
        )
        return 2

    executable = str(Path(sys.argv[1]).resolve())

    if not os.path.isfile(executable):
        raise FileNotFoundError(executable)

    backends = []
    proxy_process = None

    try:
        for _ in range(3):
            backends.append(start_backend())

        backend_ports = [
            server.server_address[1]
            for server, _ in backends
        ]

        failed_port = backend_ports[1]

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "health_failover.yaml"

            with socket.socket() as sock:
                sock.bind(("127.0.0.1", 0))
                proxy_port = sock.getsockname()[1]

            config = f"""
server:
  host: 127.0.0.1
  port: {proxy_port}

load_balancing:
  strategy: round_robin

health_check:
  interval_seconds: 1
  timeout_seconds: 1
  failure_threshold: 1
  success_threshold: 1

request:
  timeout_seconds: 5
  max_retries: 1

backends:
""".lstrip()

            for port in backend_ports:
                config += (
                    f"  - host: 127.0.0.1\n"
                    f"    port: {port}\n"
                    f"    weight: 1\n"
                )

            config_path.write_text(config, encoding="utf-8")

            proxy_process = subprocess.Popen(
                [executable, str(config_path)],
            )

            for port in backend_ports:
                wait_for_backend_state(
                    proxy_port,
                    port,
                    True,
                    proxy_process,
                )

            print("PASS: All three backends initially healthy")

            failed_index = backend_ports.index(failed_port)
            failed_server, failed_thread = backends[failed_index]

            stop_backend(failed_server, failed_thread)
            backends[failed_index] = (None, None)

            wait_for_backend_state(
                proxy_port,
                failed_port,
                False,
                proxy_process,
            )

            print(f"PASS: Backend {failed_port} marked unhealthy")

            counts = Counter()

            for _ in range(6):
                status, body = http_get(
                    f"http://127.0.0.1:{proxy_port}/failover"
                )

                if status != 200:
                    raise AssertionError(
                        f"Expected HTTP 200, got {status}: {body}"
                    )

                if not body.startswith("Backend "):
                    raise AssertionError(
                        f"Unexpected response: {body}"
                    )

                port = int(body.split()[1])

                if port == failed_port:
                    raise AssertionError(
                        f"Proxy sent a request to unhealthy backend "
                        f"{failed_port}"
                    )

                counts[port] += 1

            if sum(counts.values()) != 6:
                raise AssertionError(
                    f"Expected 6 successful requests, got {counts}"
                )

            print("PASS: Requests continue through healthy backends")

            recovered_server, recovered_thread = start_backend(
                failed_port
            )
            backends[failed_index] = (
                recovered_server,
                recovered_thread,
            )

            wait_for_backend_state(
                proxy_port,
                failed_port,
                True,
                proxy_process,
            )

            print(f"PASS: Backend {failed_port} recovered")

            recovered_counts = Counter()

            for _ in range(6):
                status, body = http_get(
                    f"http://127.0.0.1:{proxy_port}/recovery"
                )

                if status != 200:
                    raise AssertionError(
                        f"Expected HTTP 200 after recovery, "
                        f"got {status}: {body}"
                    )

                port = int(body.split()[1])
                recovered_counts[port] += 1

            if recovered_counts[failed_port] == 0:
                raise AssertionError(
                    f"Recovered backend {failed_port} did not receive "
                    f"any requests: {recovered_counts}"
                )

            print("PASS: Recovered backend receives requests again")
            print("PASS: Backend failure and recovery integration test")

    finally:
        if proxy_process is not None:
            if proxy_process.poll() is None:
                proxy_process.terminate()

                try:
                    proxy_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proxy_process.kill()
                    proxy_process.wait(timeout=5)

        for server, thread in backends:
            if server is not None:
                stop_backend(server, thread)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
