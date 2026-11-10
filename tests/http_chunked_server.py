from http.server import BaseHTTPRequestHandler, HTTPServer

class Handler(BaseHTTPRequestHandler):
    protocol_version = "HTTP/1.1"

    def do_GET(self):
        self.send_response(200)
        self.send_header("Transfer-Encoding", "chunked")
        self.end_headers()
        self.wfile.write(b"5\r\nbrise\r\n8\r\n-chunked\r\n0\r\n\r\n")
        self.wfile.flush()

    def log_message(self, *_args):
        pass

HTTPServer(("127.0.0.1", 18444), Handler).serve_forever()
