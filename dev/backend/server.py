from http.server import BaseHTTPRequestHandler, HTTPServer

class BackendHandler(BaseHTTPRequestHandler):
    def do_GET(self):
        body = (
            "Hello from Backend Server!\n"
            f"Backend received path: {self.path}\n"
        ).encode()

        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, format, *args):
        print(f"[Backend] {format % args}")


server = HTTPServer(("127.0.0.1", 9001), BackendHandler)

print("[Backend] Listening on 127.0.0.1:9001")
server.serve_forever()