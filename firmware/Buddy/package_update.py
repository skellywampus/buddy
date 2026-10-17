#!/usr/bin/env python3
"""Wrap a compiled Buddy app image as one SD-card update file.

The output is for Waveshare ESP32-S3-ePaper-1.54 V2 only. Users copy the
resulting BUDDY.UPD to the card root; they never run this script themselves.
"""
import argparse
import hashlib
from pathlib import Path
import re
import struct

HERE = Path(__file__).resolve().parent
HEADER = HERE / "buddy_update_format.h"
FORMAT = "<8sI16sII32s"
TARGET = b"WS154V2-BW"
LIMIT = 0x250000


def release_number():
    match = re.search(r"^#define BUDDY_FIRMWARE_VERSION (\d+)UL$", HEADER.read_text(), re.M)
    if not match:
        raise ValueError("Buddy release version is missing")
    return int(match.group(1))


def package(image: bytes) -> bytes:
    if len(image) < 64 or image[0] != 0xE9 or image[12:14] != b"\x09\x00":
        raise ValueError("input must be an ESP32-S3 application .bin image")
    if f"BUDDY:WS154V2-BW:{release_number()}UL".encode() not in image:
        raise ValueError("image lacks the matching Buddy board/release marker")
    if len(image) > LIMIT:
        raise ValueError("firmware does not fit the Buddy update slot")
    prefix = struct.pack(FORMAT, b"BUDDYUP1", 1, TARGET, release_number(),
                         len(image), hashlib.sha256(image).digest())
    assert len(prefix) == 68
    return prefix + image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("image", type=Path, help="compiled Buddy.ino.bin")
    parser.add_argument("output", type=Path, help="output BUDDY.UPD")
    args = parser.parse_args()
    data = package(args.image.read_bytes())
    args.output.write_bytes(data)
    print(f"Created {args.output} ({len(data)} bytes, release {release_number()})")


if __name__ == "__main__":
    main()
