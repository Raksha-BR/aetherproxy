from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from threading import Event, Thread


class BackendHandler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        if self.path == "/health":
            body = b"healthy\n"
        else:
            body = (
                f"Backend {self.server.server_port} received: {self.path}\n"
            ).encode()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        print(f"[Backend {self.server.server_port}] {format % args}")


def main():
    servers = []

    for port in [9001, 9002, 9004]:
        server = ThreadingHTTPServer(("127.0.0.1", port), BackendHandler)
        servers.append(server)

        Thread(target=server.serve_forever, daemon=True).start()

        print(f"Backend listening on port {port}")

    try:
        Event().wait()
    except KeyboardInterrupt:
        print("Stopping backends...")
    finally:
        for server in servers:
            server.shutdown()
            server.server_close()


if __name__ == "__main__":
    main()
