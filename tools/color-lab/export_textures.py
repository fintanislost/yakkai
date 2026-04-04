#!/usr/bin/env python3
"""Export WE .tex to PNG via LZ4 decompress + DDS header + ImageMagick.

Usage: PYENV_VERSION=3.12.9 python3 tools/color-lab/export_textures.py

Uses the wallpaperinfo tools as reference for the .tex binary format.
"""
import struct, subprocess, sys
from pathlib import Path

import lz4.block

UNPACKED = Path("/home/peter/repos/wallpaperinfo/pkg_extract/unpacked/materials")
OUT = Path("/home/peter/repos/yakkai/tools/color-lab/textures")

# WE format ID → DDS FourCC (from WPTexImageParser.cpp)
FOURCC = {4: b'DXT5', 6: b'DXT3', 7: b'DXT1'}


def create_dds_header(w, h, fourcc):
    return struct.pack("<4sIIIIIII44sII4sIIIIIIIIII",
        b'DDS ', 124, 0x00081007, h, w, 0, 0, 1,
        b'\x00'*44, 32, 0x4, fourcc, 0, 0, 0, 0, 0,
        0x1000, 0, 0, 0, 0)


def export_tex(tex_path, out_dir):
    data = tex_path.read_bytes()
    texi_idx = data.find(b'TEXI')
    texb_idx = data.find(b'TEXB')
    if texi_idx < 0 or texb_idx < 0:
        return False, "no TEXI/TEXB"

    fmt_id = struct.unpack_from('<I', data, texi_idx + 9)[0]
    w = struct.unpack_from('<I', data, texi_idx + 17)[0]
    h = struct.unpack_from('<I', data, texi_idx + 21)[0]

    fourcc = FOURCC.get(fmt_id)
    if not fourcc:
        return False, f"format {fmt_id} not DXT"

    # TEXB metadata: 9 bytes header, then fields at known offsets
    meta = texb_idx + 9
    decomp_size = struct.unpack_from('<I', data, meta + 28)[0]
    comp_size = struct.unpack_from('<I', data, meta + 32)[0]
    payload_start = meta + 36
    compressed = data[payload_start:payload_start + comp_size]

    try:
        raw = lz4.block.decompress(compressed, uncompressed_size=decomp_size)
    except Exception:
        return False, "LZ4 decompress failed"

    # Write DDS with decompressed pixel data
    dds_path = out_dir / (tex_path.stem + ".dds")
    hdr = create_dds_header(w, h, fourcc)
    dds_path.write_bytes(hdr + raw)

    # Convert to PNG via ImageMagick
    png_path = out_dir / (tex_path.stem + ".png")
    result = subprocess.run(
        ["magick", str(dds_path), str(png_path)],
        capture_output=True, text=True)
    if result.returncode != 0:
        return False, f"magick failed: {result.stderr[:100]}"

    # Also make a thumbnail
    thumb = out_dir / (tex_path.stem + "_thumb.png")
    subprocess.run(
        ["magick", str(png_path), "-resize", "400x400>", str(thumb)],
        capture_output=True)

    dds_path.unlink()  # cleanup DDS
    return True, f"{w}x{h}"


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    ok = 0
    for tex in sorted(UNPACKED.glob("*.tex")):
        name = tex.stem
        success, msg = export_tex(tex, OUT)
        if success:
            print(f"  [+] {name}: {msg}")
            ok += 1
        else:
            print(f"  [-] {name}: {msg}")
    print(f"\nExported {ok} textures to {OUT}/")


if __name__ == "__main__":
    main()
