#!/usr/bin/env python3
"""Local web server that turns browser-prepared samples into a custom UF2."""

from http import HTTPStatus
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
import base64
import json
import mimetypes
import posixpath
import subprocess
import sys
import time
from urllib.parse import unquote, urlsplit
import uuid


ROOT = Path(__file__).resolve().parents[1]
WORK_DIR = ROOT / "build-web"
UPLOAD_DIR = WORK_DIR / "uploads"
BUILD_DIR = WORK_DIR / "cmake"
OUTPUT_DIR = WORK_DIR / "uf2"
SAMPLE_FILENAMES = {
    "marquee_oi.wav",
    "cbgb_hey_ho.wav",
    "club100_no_future.wav",
    "whisky_lets_go.wav",
}


class PunkConfusionHandler(SimpleHTTPRequestHandler):
    server_version = "PunkConfusionUF2/0.1"

    def translate_path(self, path):
        if path == "/":
            path = "/web/index.html"
        clean_path = posixpath.normpath(unquote(urlsplit(path).path)).lstrip("/")
        if clean_path.startswith("../") or clean_path == "..":
            return str(ROOT / "web" / "index.html")
        return str(ROOT / clean_path)

    def do_GET(self):
        if self.path == "/":
            self.send_response(HTTPStatus.FOUND)
            self.send_header("Location", "/web/")
            self.end_headers()
            return
        super().do_GET()

    def do_POST(self):
        if self.path != "/api/build":
            self.send_error(HTTPStatus.NOT_FOUND, "Unknown endpoint")
            return

        try:
            payload = self.read_json_payload()
            uf2_path, _log = self.build_custom_uf2(payload)
            self.send_response(HTTPStatus.OK)
            self.send_header("Content-Type", "application/octet-stream")
            self.send_header("Content-Length", str(uf2_path.stat().st_size))
            self.send_header(
                "Content-Disposition",
                'attachment; filename="punk_confusion_custom.uf2"',
            )
            self.end_headers()
            with uf2_path.open("rb") as uf2:
                self.wfile.write(uf2.read())
        except Exception as error:
            self.send_json(
                HTTPStatus.BAD_REQUEST,
                {"ok": False, "error": str(error)},
            )

    def guess_type(self, path):
        if path.endswith(".js"):
            return "text/javascript"
        return mimetypes.guess_type(path)[0] or "application/octet-stream"

    def read_json_payload(self):
        length = int(self.headers.get("Content-Length", "0"))
        if length <= 0:
            raise ValueError("Missing request body")
        if length > 8 * 1024 * 1024:
            raise ValueError("Request is too large; keep shouts short")

        raw = self.rfile.read(length)
        return json.loads(raw.decode("utf-8"))

    def build_custom_uf2(self, payload):
        samples = payload.get("samples")
        if not isinstance(samples, list) or len(samples) != len(SAMPLE_FILENAMES):
            raise ValueError("Expected four samples")

        build_id = time.strftime("%Y%m%d-%H%M%S") + "-" + uuid.uuid4().hex[:8]
        sample_dir = UPLOAD_DIR / build_id
        cmake_dir = BUILD_DIR / build_id
        output_path = OUTPUT_DIR / f"punk_confusion_custom_{build_id}.uf2"
        sample_dir.mkdir(parents=True, exist_ok=True)
        OUTPUT_DIR.mkdir(parents=True, exist_ok=True)

        seen = set()
        for sample in samples:
            filename = sample.get("filename")
            encoded = sample.get("data")
            if filename not in SAMPLE_FILENAMES:
                raise ValueError(f"Unexpected sample filename: {filename}")
            if filename in seen:
                raise ValueError(f"Duplicate sample filename: {filename}")
            if not isinstance(encoded, str):
                raise ValueError(f"Missing sample data for {filename}")

            seen.add(filename)
            (sample_dir / filename).write_bytes(base64.b64decode(encoded))

        missing = SAMPLE_FILENAMES - seen
        if missing:
            raise ValueError(f"Missing samples: {', '.join(sorted(missing))}")

        command = [
            sys.executable,
            "tools/build_custom_uf2.py",
            "--samples",
            str(sample_dir),
            "--build-dir",
            str(cmake_dir),
            "--output",
            str(output_path),
            "--clean",
        ]
        result = subprocess.run(
            command,
            cwd=ROOT,
            text=True,
            capture_output=True,
            check=False,
        )
        log = result.stdout + result.stderr
        if result.returncode != 0:
            raise RuntimeError(log.strip() or "UF2 build failed")
        if not output_path.exists():
            raise FileNotFoundError(f"Expected UF2 not found: {output_path}")
        return output_path, log

    def send_json(self, status, body):
        encoded = json.dumps(body).encode("utf-8")
        self.send_response(status)
        self.send_header("Content-Type", "application/json")
        self.send_header("Content-Length", str(len(encoded)))
        self.end_headers()
        self.wfile.write(encoded)


def main():
    port = int(sys.argv[1]) if len(sys.argv) > 1 else 8765
    address = ("127.0.0.1", port)
    print(f"Punk Confusion UF2 builder running at http://{address[0]}:{address[1]}/web/")
    print("Press Ctrl-C to stop.")
    ThreadingHTTPServer(address, PunkConfusionHandler).serve_forever()


if __name__ == "__main__":
    main()
