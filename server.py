#!/usr/bin/env python3
"""Tiny local server that serves rectangle.html and persists circle
coordinates to coords.json in the same folder.

Usage:
    python server.py            # serves on http://localhost:8000
    python server.py 8080       # custom port

Open the printed URL in any browser. Coordinates are saved on every
change and reloaded automatically. Press Ctrl+C to stop.
"""

import json
import os
import sys
from http.server import HTTPServer, SimpleHTTPRequestHandler
from urllib.parse import urlparse

ROOT = os.path.dirname(os.path.abspath(__file__))
COORDS_FILE = os.path.join(ROOT, "coords.json")

# Hard caps used by /coords POST validation
MAX_BODY_BYTES   = 1_000_000   # ~1 MB; the real payload is well under 1 KB
MAX_ITEMS        =     1_000   # plenty above the 8 circles we use
COORD_MIN, COORD_MAX = -10_000, 10_000   # coords are mm in viewBox space
DIAM_MIN,  DIAM_MAX  =       0,  1_000


class Handler(SimpleHTTPRequestHandler):
    def __init__(self, *args, **kwargs):
        super().__init__(*args, directory=ROOT, **kwargs)

    # always send no-cache + permissive CORS so Firefox can't show stale state
    def end_headers(self):
        self.send_header("Cache-Control", "no-store, no-cache, must-revalidate, max-age=0")
        self.send_header("Pragma", "no-cache")
        self.send_header("Expires", "0")
        self.send_header("Access-Control-Allow-Origin", "*")
        self.send_header("Access-Control-Allow-Methods", "GET, POST, OPTIONS")
        self.send_header("Access-Control-Allow-Headers", "Content-Type")
        super().end_headers()

    def do_OPTIONS(self):
        self.send_response(204)
        self.end_headers()

    def _send_json(self, body, status=200):
        if isinstance(body, (dict, list)) or body is None:
            body = json.dumps(body)
        if isinstance(body, str):
            body = body.encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def do_GET(self):
        path = urlparse(self.path).path
        if path in ("/", ""):
            self.path = "/rectangle.html"
            return super().do_GET()
        if path == "/coords":
            try:
                with open(COORDS_FILE, "r", encoding="utf-8") as f:
                    body = f.read().strip() or "null"
                return self._send_json(body)
            except FileNotFoundError:
                return self._send_json("null")
            except Exception as e:
                return self._send_json({"error": str(e)}, status=500)
        return super().do_GET()

    def do_POST(self):
        path = urlparse(self.path).path
        if path != "/coords":
            self.send_response(404)
            self.end_headers()
            return
        try:
            length = int(self.headers.get("Content-Length", "0") or "0")
        except ValueError:
            return self._send_json({"error": "bad Content-Length"}, status=400)
        if length < 0 or length > MAX_BODY_BYTES:
            return self._send_json(
                {"error": f"body too large ({length} > {MAX_BODY_BYTES})"},
                status=413,
            )
        raw = self.rfile.read(length) if length > 0 else b""
        try:
            data = json.loads(raw.decode("utf-8"))
            if not isinstance(data, list):
                raise ValueError("expected a JSON array")
            if len(data) > MAX_ITEMS:
                raise ValueError(f"too many items ({len(data)} > {MAX_ITEMS})")
            for i, item in enumerate(data):
                if not isinstance(item, dict):
                    raise ValueError(f"item {i} is not an object")
                for key, lo, hi in (("d", DIAM_MIN, DIAM_MAX),
                                    ("cx", COORD_MIN, COORD_MAX),
                                    ("cy", COORD_MIN, COORD_MAX)):
                    v = item.get(key)
                    if not isinstance(v, (int, float)) or isinstance(v, bool):
                        raise ValueError(f"item {i}: {key!r} must be a number")
                    # NaN/inf check (NaN != NaN, inf is out of bounds)
                    if v != v or v < lo or v > hi:
                        raise ValueError(f"item {i}: {key!r}={v!r} out of range [{lo}, {hi}]")
            tmp = COORDS_FILE + ".tmp"
            with open(tmp, "w", encoding="utf-8") as f:
                json.dump(data, f, indent=2)
            os.replace(tmp, COORDS_FILE)
            return self._send_json({"ok": True})
        except Exception as e:
            return self._send_json({"error": str(e)}, status=400)

    def log_message(self, fmt, *args):
        # quieter logs; skip the high-frequency /coords POSTs
        msg = fmt % args
        if "/coords" in msg:
            return
        sys.stderr.write("%s - %s\n" % (self.address_string(), msg))


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8000
    try:
        httpd = HTTPServer(("127.0.0.1", port), Handler)
    except OSError as e:
        print(f"Could not bind to port {port}: {e}", file=sys.stderr)
        print(f"  Try another port:  python {os.path.basename(sys.argv[0])} 8080",
              file=sys.stderr)
        sys.exit(1)
    print(f"Serving folder: {ROOT}")
    print(f"Coordinates file: {COORDS_FILE}")
    print(f"Open http://localhost:{port} in your browser.")
    print("Press Ctrl+C to stop.")
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        print("\nStopped.")


if __name__ == "__main__":
    main()
