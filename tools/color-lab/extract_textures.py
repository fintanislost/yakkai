#!/usr/bin/env python3
"""Extract WE .tex files to PNG for the color lab tool.

Usage: PYENV_VERSION=3.12.9 python3 tools/color-lab/extract_textures.py <unpacked_dir> <output_dir>
"""
import struct, sys, os
from pathlib import Path
from io import BytesIO

import lz4.block
from PIL import Image


def decode_bc3_block(data, offset):
    """Decode one 4x4 BC3 block (16 bytes) into 16 RGBA pixels."""
    a0, a1 = data[offset], data[offset + 1]
    alpha_bits = int.from_bytes(data[offset + 2:offset + 8], 'little')
    alphas = [a0, a1, 0, 0, 0, 0, 0, 0]
    if a0 > a1:
        for i in range(1, 7):
            alphas[i + 1] = ((7 - i) * a0 + i * a1) // 7
    else:
        for i in range(1, 5):
            alphas[i + 1] = ((5 - i) * a0 + i * a1) // 5
        alphas[6], alphas[7] = 0, 255

    c0 = struct.unpack_from('<H', data, offset + 8)[0]
    c1 = struct.unpack_from('<H', data, offset + 10)[0]
    bits = struct.unpack_from('<I', data, offset + 12)[0]
    def rgb565(v):
        return (((v>>11)&0x1F)*255//31, ((v>>5)&0x3F)*255//63, (v&0x1F)*255//31)
    colors = [rgb565(c0), rgb565(c1)]
    colors.append(tuple((2*colors[0][i]+colors[1][i])//3 for i in range(3)))
    colors.append(tuple((colors[0][i]+2*colors[1][i])//3 for i in range(3)))

    pixels = []
    for i in range(16):
        r, g, b = colors[(bits >> (i*2)) & 3]
        a = alphas[(alpha_bits >> (i*3)) & 7]
        pixels.append((r, g, b, a))
    return pixels


class BinaryReader:
    def __init__(self, data):
        self.data = data
        self.pos = 0

    def read(self, n):
        r = self.data[self.pos:self.pos+n]
        self.pos += n
        return r

    def read_int32(self):
        v = struct.unpack_from('<i', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_uint32(self):
        v = struct.unpack_from('<I', self.data, self.pos)[0]
        self.pos += 4
        return v

    def read_version(self):
        """Read TEXV/TEXI/TEXB version header: "XXXX0005\0" -> returns 5"""
        sig = self.data[self.pos:self.pos+4]
        ver_str = self.data[self.pos+4:self.pos+8].decode('ascii')
        self.pos += 9  # 8 chars + null
        return int(ver_str)


def read_tex(path):
    with open(path, 'rb') as f:
        data = f.read()

    r = BinaryReader(data)

    # Header (matches WPTexImageParser::LoadHeader)
    texv_ver = r.read_version()  # TEXV
    texi_ver = r.read_version()  # TEXI

    fmt = r.read_int32()         # format
    flags = r.read_uint32()      # flags
    width = r.read_int32()       # width
    height = r.read_int32()      # height
    map_w = r.read_int32()       # mapWidth
    map_h = r.read_int32()       # mapHeight
    r.read_int32()               # unknown

    texb_ver = r.read_version()  # TEXB
    img_count = r.read_int32()   # count

    if texb_ver == 3:
        r.read_int32()  # image type

    if texb_ver >= 4:
        # TEXB v4 has 8 extra bytes then per-image: fmt, w, h, lz4, decomp, src
        r.read_int32()  # 0xFFFFFFFF flags
        r.read_int32()  # 0x00000000 reserved
        fmt = r.read_int32()
        mip_w = r.read_int32()
        mip_h = r.read_int32()
        lz4_compressed = r.read_int32() == 1
        decompressed_size = r.read_int32()
        src_size = r.read_int32()
    else:
        mip_count = max(r.read_int32(), 0)
        if mip_count == 0:
            return None
        mip_w = r.read_int32()
        mip_h = r.read_int32()
        lz4_compressed = False
        decompressed_size = 0
        if texb_ver > 1:
            lz4_compressed = r.read_int32() == 1
            decompressed_size = r.read_int32()
        src_size = r.read_int32()

    raw_data = r.read(src_size)

    if lz4_compressed and decompressed_size > 0:
        raw_data = lz4.block.decompress(raw_data, uncompressed_size=decompressed_size)

    # Decode BC3/DXT5
    if fmt == 4:  # BC3
        img = Image.new('RGBA', (mip_w, mip_h))
        pixels = img.load()
        block_w = (mip_w + 3) // 4
        block_h = (mip_h + 3) // 4
        for by in range(block_h):
            for bx in range(block_w):
                off = (by * block_w + bx) * 16
                if off + 16 > len(raw_data):
                    break
                block = decode_bc3_block(raw_data, off)
                for i, (cr, cg, cb, ca) in enumerate(block):
                    px, py = bx*4 + i%4, by*4 + i//4
                    if px < mip_w and py < mip_h:
                        pixels[px, py] = (cr, cg, cb, ca)
        return img, mip_w, mip_h
    elif fmt == 0:  # RGBA8
        img = Image.frombytes('RGBA', (mip_w, mip_h), bytes(raw_data[:mip_w*mip_h*4]))
        return img, mip_w, mip_h
    else:
        return None


def main():
    if len(sys.argv) < 3:
        print(f"Usage: {sys.argv[0]} <unpacked_materials_dir> <output_dir>")
        sys.exit(1)

    materials_dir = Path(sys.argv[1])
    output_dir = Path(sys.argv[2])
    output_dir.mkdir(parents=True, exist_ok=True)

    for tex_file in sorted(materials_dir.glob("*.tex")):
        name = tex_file.stem
        print(f"  {name}...", end="", flush=True)
        try:
            result = read_tex(tex_file)
            if result:
                img, w, h = result
                out = output_dir / f"{name}.png"
                img.save(out)
                thumb = img.copy()
                thumb.thumbnail((400, 400))
                thumb.save(output_dir / f"{name}_thumb.png")
                print(f" {w}x{h} → {out.stat().st_size // 1024}KB")
            else:
                print(" unsupported format")
        except Exception as e:
            print(f" ERROR: {e}")


if __name__ == "__main__":
    main()
