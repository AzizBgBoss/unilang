from http.server import BaseHTTPRequestHandler, HTTPServer

class H(BaseHTTPRequestHandler):
    def do_POST(self):
        n = int(self.headers.get("Content-Length", 0))
        body = self.rfile.read(n)
        print(f"POST {self.path} headers={dict(self.headers)} body={body!r}")
        out = b"ok: " + body
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(out)))
        self.end_headers()
        self.wfile.write(out)

    do_GET = lambda self: (self.send_response(200), self.send_header("Content-Length", "2"), self.end_headers(), self.wfile.write(b"ok"))

HTTPServer(("127.0.0.1", 8080), H).serve_forever()
