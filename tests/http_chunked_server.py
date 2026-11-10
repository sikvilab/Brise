from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        self.wfile.write(bytes.fromhex("35 0d 0a 62 72 69 73 65 0d 0a 38 0d 0a 2d 63 68 75 6e 6b 65 64 0d 0a 30 0d 0a 0d 0a"))
        self.wfile.flush()

    def log_message(self, *_args):
        pass

HTTPServer(("127.0.0.1", 18444), Handler).serve_forever()
