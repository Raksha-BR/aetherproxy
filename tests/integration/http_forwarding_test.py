import os
import socket
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.request
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path


class BackendHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/health":
            body = b"healthy\n"
        else:
            body = (f"Integration backend received: {self.path}\n").encode()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        pass


def get_free_port():
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return sock.getsockname()[1]


def http_get(url, timeout=1):
    try:
        with urllib.request.urlopen(url, timeout=timeout) as response:
            return response.status, response.read().decode()
    except urllib.error.HTTPError as error:
        return error.code, error.read().decode()


def wait_for_healthy_backend(proxy_port, backend_port, process):
    metrics_url = f"http://127.0.0.1:{proxy_port}/metrics"
    expected_metric = (
        f'aetherproxy_backend_healthy{{backend="127.0.0.1:{backend_port}"}} 1'
    )

    deadline = time.monotonic() + 15

    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(
                f"Proxy exited unexpectedly with code {process.returncode}"
            )

        try:
            status, metrics = http_get(metrics_url)

            if status == 200 and expected_metric in metrics:
                return

        except (OSError, urllib.error.URLError):
            pass

        time.sleep(0.2)

    raise TimeoutError("Backend did not become healthy within 15 seconds")


def main():
    if len(sys.argv) != 2:
        print(
            "Usage: http_forwarding_test.py <path-to-aetherproxy>",
            file=sys.stderr,
        )
        return 2

    executable = str(Path(sys.argv[1]).resolve())

    if not os.path.isfile(executable):
        print(
            f"Proxy executable not found: {executable}",
            file=sys.stderr,
        )
        return 2

    backend = ThreadingHTTPServer(
        ("127.0.0.1", 0),
        BackendHandler,
    )
    backend_port = backend.server_address[1]

    backend_thread = threading.Thread(
        target=backend.serve_forever,
        daemon=True,
    )
    backend_thread.start()

    proxy_process = None

    try:
        proxy_port = get_free_port()

        with tempfile.TemporaryDirectory() as temp_dir:
            config_path = Path(temp_dir) / "integration.yaml"

            config_path.write_text(
                f"""
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
  - host: 127.0.0.1
    port: {backend_port}
    weight: 1
""".strip()
                + "\n",
                encoding="utf-8",
            )

            # Inherit output so proxy errors are visible during test failures.
            proxy_process = subprocess.Popen(
                [executable, str(config_path)],
            )

            wait_for_healthy_backend(
                proxy_port,
                backend_port,
                proxy_process,
            )

            status, body = http_get(
                f"http://127.0.0.1:{proxy_port}/integration-test",
                timeout=5,
            )

            expected_body = "Integration backend received: /integration-test\n"

            if status != 200:
                raise AssertionError(
                    f"Expected HTTP 200, received HTTP {status}. Response: {body!r}"
                )

            if body != expected_body:
                raise AssertionError(f"Unexpected response body: {body!r}")

            print("PASS: HTTP request forwarded through AetherProxy")
            print(f"     Proxy port:   {proxy_port}")
            print(f"     Backend port: {backend_port}")
            print(f"     Response:     {body.strip()}")

    finally:
        if proxy_process is not None:
            if proxy_process.poll() is None:
                proxy_process.terminate()

                try:
                    proxy_process.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    proxy_process.kill()
                    proxy_process.wait(timeout=5)

        backend.shutdown()
        backend.server_close()
        backend_thread.join(timeout=5)


if __name__ == "__main__":
    try:
        sys.exit(main())
    except Exception as error:  # noqa: BLE001
        print(f"FAIL: {error}", file=sys.stderr)
        sys.exit(1)
