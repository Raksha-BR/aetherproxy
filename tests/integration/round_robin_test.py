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


def http_get(url, timeout=2):
    with urllib.request.urlopen(url, timeout=timeout) as response:
        return response.status, response.read().decode().strip()


def wait_for_backends(proxy_port, backend_ports, process):
    url = f"http://127.0.0.1:{proxy_port}/metrics"
    expected = [
        f'aetherproxy_backend_healthy{{backend="127.0.0.1:{port}"}} 1'
        for port in backend_ports
    ]

    deadline = time.monotonic() + 15

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"Proxy exited with code {process.returncode}")

        try:
            status, metrics = http_get(url)

            if status == 200 and all(metric in metrics for metric in expected):
                return

        except (OSError, urllib.error.URLError):
            pass

        time.sleep(0.2)

    raise TimeoutError("Not all backends became healthy")


def main():
    if len(sys.argv) != 2:
        print(
            "Usage: round_robin_test.py <path-to-aetherproxy>",
            file=sys.stderr,
        )
        return 2

    executable = str(Path(sys.argv[1]).resolve())

    if not os.path.isfile(executable):
        raise FileNotFoundError(executable)

    backends = []
    backend_threads = []
    proxy_process = None

    try:
        for _ in range(3):
            server = ThreadingHTTPServer(
                ("127.0.0.1", 0),
                BackendHandler,
            )
            backends.append(server)

            thread = threading.Thread(
                target=server.serve_forever,
                daemon=True,
            )
            thread.start()
            backend_threads.append(thread)

        backend_ports = [server.server_address[1] for server in backends]

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "round_robin.yaml"
            proxy_port = get_free_port()

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
                config += f"  - host: 127.0.0.1\n    port: {port}\n    weight: 1\n"

            config_path.write_text(config, encoding="utf-8")

            proxy_process = subprocess.Popen(
                [executable, str(config_path)],
            )

            wait_for_backends(
                proxy_port,
                backend_ports,
                proxy_process,
            )

            counts = Counter()

            for _ in range(6):
                status, body = http_get(f"http://127.0.0.1:{proxy_port}/round-robin")

                if status != 200:
                    raise AssertionError(f"Expected HTTP 200, got {status}")

                if not body.startswith("Backend "):
                    raise AssertionError(f"Unexpected backend response: {body}")

                port = int(body.split()[1])
                counts[port] += 1

            expected = Counter({port: 2 for port in backend_ports})

            if counts != expected:
                raise AssertionError(f"Expected {expected}, received {counts}")

            print("PASS: Round-robin distributes requests evenly")
            for port in backend_ports:
                print(f"     Backend {port}: {counts[port]} requests")

    finally:
        if proxy_process is not None:
            if proxy_process.poll() is None:
                proxy_process.terminate()

                try:
                    proxy_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proxy_process.kill()
                    proxy_process.wait(timeout=5)

        for server in backends:
            server.shutdown()
            server.server_close()

        for thread in backend_threads:
            thread.join(timeout=5)


def get_free_port():
    import socket

    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
