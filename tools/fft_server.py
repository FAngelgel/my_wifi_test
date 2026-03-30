#!/usr/bin/env python3
import argparse
import json
import sys
from http.server import BaseHTTPRequestHandler, HTTPServer
from datetime import datetime


class Handler(BaseHTTPRequestHandler):
    server_version = "fft_server/0.1"

    def _send(self, code: int, body: str, content_type: str = "text/plain; charset=utf-8"):
        data = body.encode("utf-8")
        self.send_response(code)
        self.send_header("Content-Type", content_type)
        self.send_header("Content-Length", str(len(data)))
        self.end_headers()
        self.wfile.write(data)

    def do_GET(self):
        if self.path in ("/", "/health"):
            self._send(200, "ok\n")
            return
        self._send(404, "not found\n")

    def do_POST(self):
        if self.path != "/fft":
            self._send(404, "not found\n")
            return

        try:
            length = int(self.headers.get("Content-Length", "0"))
        except ValueError:
            self._send(400, "bad content-length\n")
            return

        raw = self.rfile.read(length) if length > 0 else b""
        try:
            payload = json.loads(raw.decode("utf-8") if raw else "{}")
        except Exception as e:
            self._send(400, f"invalid json: {e}\n")
            return

        ts = datetime.now().strftime("%Y-%m-%d %H:%M:%S")
        if getattr(self.server, "quiet", False):
            pass
        else:
            print(f"[{ts}] /fft from {self.client_address[0]}:{self.client_address[1]}")
            print(json.dumps(payload, ensure_ascii=False, indent=2))

        out_path = getattr(self.server, "out_path", None)
        if out_path:
            try:
                with open(out_path, "a", encoding="utf-8") as f:
                    f.write(json.dumps({"ts": ts, "remote": self.client_address[0], "payload": payload}, ensure_ascii=False))
                    f.write("\n")
            except Exception as e:
                self._send(500, f"failed to write output: {e}\n")
                return

        self._send(200, "ok\n")

    def log_message(self, format, *args):
        if getattr(self.server, "quiet", False):
            return
        super().log_message(format, *args)


def main(argv: list[str]) -> int:
    ap = argparse.ArgumentParser(description="Local HTTP server to receive Fourier/FFT results")
    ap.add_argument("--bind", default="0.0.0.0", help="bind address (default: 0.0.0.0)")
    ap.add_argument("--port", type=int, default=8080, help="port (default: 8080)")
    ap.add_argument("--out", default="", help="append JSONL to this file")
    ap.add_argument("--quiet", action="store_true", help="reduce logging")
    args = ap.parse_args(argv)

    httpd = HTTPServer((args.bind, args.port), Handler)
    httpd.out_path = args.out or None
    httpd.quiet = bool(args.quiet)

    print(f"Listening on http://{args.bind}:{args.port}")
    print("Endpoints: GET /health, POST /fft")
    print("Example:")
    print(
        "  curl -X POST http://127.0.0.1:8080/fft -H \"Content-Type: application/json\" "
        "-d \"{\\\"sample_rate_hz\\\":16000,\\\"n\\\":1024,\\\"peak_hz\\\":1000.0}\""
    )
    try:
        httpd.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        httpd.server_close()
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv[1:]))

