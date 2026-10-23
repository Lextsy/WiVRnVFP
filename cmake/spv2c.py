#!/usr/bin/env python3
"""Convert a binary SPIR-V module into a C array initializer of uint32 words.

SPIR-V is a sequence of 32-bit little-endian words. The shaders map is
std::vector<uint32_t>, so each emitted element must be one word (4 bytes),
not one byte. The previous hexdump-based generation emitted one uint32 per
byte, producing an invalid magic number (0x00000003) at runtime.
"""
import struct
import sys

if len(sys.argv) != 2:
    sys.stderr.write("usage: spv2c.py <input.spv>\n")
    sys.exit(2)

with open(sys.argv[1], "rb") as f:
    data = f.read()

if len(data) % 4:
    sys.stderr.write("spv2c.py: input is not a multiple of 4 bytes\n")
    sys.exit(1)

words = struct.unpack("<%dI" % (len(data) // 4), data)
sys.stdout.write(", ".join("0x%08X" % w for w in words))
sys.stdout.write(",\n")
