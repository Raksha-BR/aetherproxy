from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
import argparse


class BackendHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = (
            f"Hello from Backend Server!\nBackend received path: {self.path}\n"
        ).encode()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        print(f"[Backend] {format % args}")


parser = argparse.ArgumentParser()
parser.add_argument("--port", type=int, default=9001)
args = parser.parse_args()

server = ThreadingHTTPServer(("127.0.0.1", args.port), BackendHandler)

print(f"[Backend] Listening on 127.0.0.1:{args.port}")
server.serve_forever()