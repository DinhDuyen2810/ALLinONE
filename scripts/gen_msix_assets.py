"""Sinh bộ logo cho gói MSIX (Microsoft Store) từ assets/app_icon.ico.

Chạy lại khi đổi biểu tượng ứng dụng:  python scripts/gen_msix_assets.py
Kết quả nằm ở installer/msix/Assets/ (được commit - build_msix.bat chỉ chép, không sinh lại, để máy build
không cần Pillow). Lấy khung 256x256 (PNG) của tệp .ico rồi thu nhỏ bằng LANCZOS, nền trong suốt.
"""
import io
import os
import struct
import sys

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
ICO = os.path.join(ROOT, "assets", "app_icon.ico")
OUT = os.path.join(ROOT, "installer", "msix", "Assets")

# Tên tệp -> (rộng, cao, cạnh biểu tượng). Kích thước đúng theo tên thuộc tính trong AppxManifest.
TARGETS = {
    "StoreLogo.png": (50, 50, 50),
    "Square44x44Logo.png": (44, 44, 44),
    "Square150x150Logo.png": (150, 150, 120),
    "Wide310x150Logo.png": (310, 150, 120),
}


def largest_frame(path):
    data = open(path, "rb").read()
    _, _, count = struct.unpack("<HHH", data[:6])
    best = None
    for i in range(count):
        w, h, _, _, _, _, size, off = struct.unpack("<BBBBHHII", data[6 + 16 * i:22 + 16 * i])
        w, h = w or 256, h or 256
        if best is None or w > best[0]:
            best = (w, data[off:off + size])
    return Image.open(io.BytesIO(best[1])).convert("RGBA")


def main():
    src = largest_frame(ICO)
    os.makedirs(OUT, exist_ok=True)
    for name, (w, h, side) in TARGETS.items():
        canvas = Image.new("RGBA", (w, h), (0, 0, 0, 0))
        icon = src.resize((side, side), Image.LANCZOS)
        canvas.paste(icon, ((w - side) // 2, (h - side) // 2), icon)
        canvas.save(os.path.join(OUT, name), optimize=True)
        print(name, w, h)
    return 0


if __name__ == "__main__":
    sys.exit(main())
