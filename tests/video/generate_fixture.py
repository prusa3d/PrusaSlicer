"""Generate a moving YUV test video without additional Python packages."""
import sys
from pathlib import Path

path = Path(sys.argv[1]).resolve()
path.parent.mkdir(parents=True, exist_ok=True)
width, height = 320, 180
with path.open("wb") as video:
    video.write(f"YUV4MPEG2 W{width} H{height} F10:1 Ip A1:1 C420jpeg\n".encode())
    for frame in range(150):
        video.write(b"FRAME\n")
        video.write(bytes(50 + ((x // 24 + y // 20 + frame) % 4) * 45
                          for y in range(height) for x in range(width)))
        video.write(bytes([100]) * (width * height // 4))
        video.write(bytes([180]) * (width * height // 4))
print(path.as_uri())
