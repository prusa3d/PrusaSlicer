"""Exercise the native decoder against a changing multipart HTTP camera stream.

Usage: python3 tests/video/http_mjpeg_fixture.py /path/to/video_stream_decoder_smoke
Requires Pillow to generate alternating JPEG test frames.
"""
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from io import BytesIO
from PIL import Image
import subprocess
import sys
import threading
import time

frames = []
for color in ("orange", "blue"):
    output = BytesIO()
    Image.new("RGB", (320, 180), color).save(output, format="JPEG")
    frames.append(output.getvalue())


class Stream(BaseHTTPRequestHandler):
    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "multipart/x-mixed-replace; boundary=camera-frame")
        self.end_headers()
        try:
            for index in range(600):
                frame = frames[index % len(frames)]
                self.wfile.write(b"--camera-frame\r\nContent-Type: image/jpeg\r\n" +
                                 f"Content-Length: {len(frame)}\r\n\r\n".encode() + frame + b"\r\n")
                self.wfile.flush()
                time.sleep(0.05)
        except (BrokenPipeError, ConnectionResetError):
            pass

    def log_message(self, *_):
        pass


if __name__ == "__main__":
    with ThreadingHTTPServer(("127.0.0.1", 0), Stream) as server:
        server.daemon_threads = True
        worker = threading.Thread(target=server.serve_forever, daemon=True)
        worker.start()
        try:
            result = subprocess.run([sys.argv[1],
                f"http://127.0.0.1:{server.server_port}/stream", "http"], timeout=60)
        finally:
            server.shutdown()
            worker.join()
        sys.exit(result.returncode)
