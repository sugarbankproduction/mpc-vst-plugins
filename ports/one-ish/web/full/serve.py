"""Serve web/site locally with the two headers the full plugin needs (its threads share memory, which browsers allow
only on a cross-origin isolated page). Run from the repo root: python ports/one-ish/web/full/serve.py [port]
then open http://127.0.0.1:8737/ (the sound-only page: /simple.html)"""
import functools
import http.server
import os
import sys


class Handler(http.server.SimpleHTTPRequestHandler):
    def end_headers(self):
        self.send_header("Cross-Origin-Opener-Policy", "same-origin")
        self.send_header("Cross-Origin-Embedder-Policy", "require-corp")
        self.send_header("Cache-Control", "no-store")
        super().end_headers()

    def do_POST(self):  # (testing) POST /__shot?name=x with a PNG: saved as build/png/web_x.png
        if not self.path.startswith("/__shot"):
            self.send_error(404)
            return
        n = int(self.headers.get("Content-Length", 0))
        origin = self.headers.get("Origin", "")
        if n > 32 << 20 or (origin and not origin.startswith("http://127.0.0.1:")):  # (this page only, a picture's size)
            self.send_error(403)
            return
        name = "".join(c for c in self.path.partition("name=")[2] if c.isalnum() or c in "_-") or "shot"
        data = self.rfile.read(n)
        out = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "..", "build", "png")
        os.makedirs(out, exist_ok=True)
        with open(os.path.join(out, f"web_{name}.png"), "wb") as f:
            f.write(data)
        self.send_response(204)
        self.end_headers()


port = int(sys.argv[1]) if len(sys.argv) > 1 else 8737
site = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "site")
http.server.ThreadingHTTPServer(("127.0.0.1", port), functools.partial(Handler, directory=site)).serve_forever()
