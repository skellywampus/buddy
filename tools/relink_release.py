#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Relink a released Buddy app against a locally modified Arduino library set.

This helper intentionally builds only a tiny library-discovery sketch. It then
reuses that build's linker command with the exact application objects supplied
for the release. It never uploads or flashes a board.
"""
import argparse
import hashlib
import json
import os
import re
import shlex
import shutil
import subprocess
import tempfile
from pathlib import Path

OBJECTS = {"Buddy.ino.cpp.o", "buddy_esp32_sr.c.o"}


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def load_manifest(folder):
    folder = folder.resolve()
    manifest = json.loads((folder / "object-manifest.json").read_text())
    if (manifest.get("fqbn") != FQBN or manifest.get("arduino_core") != "3.3.12"
            or manifest.get("toolchain") != "esp-x32 2601"):
        raise ValueError("Relink manifest does not match the pinned board/toolchain")
    if type(manifest.get("release")) is not int or manifest["release"] <= 0:
        raise ValueError("Invalid relink release")
    if not re.fullmatch(r"[0-9a-f]{64}", manifest.get("application_sha256", "")):
        raise ValueError("Invalid application fingerprint")
    if not re.fullmatch(r"(?:Jan|Feb|Mar|Apr|May|Jun|Jul|Aug|Sep|Oct|Nov|Dec) [ 0-3][0-9] [0-9]{4}", manifest.get("build_date", "")):
        raise ValueError("Invalid release build date")
    if not re.fullmatch(r"(?:[01][0-9]|2[0-3]):[0-5][0-9]:[0-5][0-9]", manifest.get("build_time", "")):
        raise ValueError("Invalid release build time")
    objects = manifest.get("objects")
    if (not isinstance(objects, list) or len(objects) != 2
            or any(not isinstance(obj, dict) for obj in objects)
            or {obj.get("file") for obj in objects} != OBJECTS):
        raise ValueError("Relink manifest must list exactly the two application objects")
    for obj in objects:
        path = folder / obj["file"]
        if path.is_symlink() or not path.is_file() or path.stat().st_size != obj.get("bytes") or sha(path) != obj.get("sha256"):
            raise ValueError("Missing or damaged application object: " + obj["file"])
    return manifest


def quoted_flag(value):
    # Arduino CLI parses this string again; retain spaces in user/temp paths.
    return '"' + str(value).replace("\\", "/").replace('"', '\\"') + '"'


def split_command(line):
    if os.name != "nt":
        return shlex.split(line)
    # The CLI prints a Windows command line, not POSIX shell quoting.
    import ctypes
    from ctypes import wintypes
    api = ctypes.windll.shell32.CommandLineToArgvW
    api.argtypes = [wintypes.LPCWSTR, ctypes.POINTER(ctypes.c_int)]
    api.restype = ctypes.POINTER(wintypes.LPWSTR)
    count = ctypes.c_int()
    argv = api(line, ctypes.byref(count))
    if not argv:
        raise ValueError("Cannot parse Arduino linker command")
    try:
        return [argv[i] for i in range(count.value)]
    finally:
        free = ctypes.windll.kernel32.LocalFree
        free.argtypes = [ctypes.c_void_p]
        free.restype = ctypes.c_void_p
        free(ctypes.cast(argv, ctypes.c_void_p))


def image_payload(data):
    """Validate an ESP-S3 image and normalize only its two digest fields."""
    if len(data) < 256 or data[0] != 0xE9 or data[12:14] != b"\x09\x00" or not 1 <= data[1] <= 16 or data[23] != 1:
        raise ValueError("Invalid ESP32-S3 image header")
    pos = 24
    checksum = 0xEF
    for _ in range(data[1]):
        if pos + 8 > len(data):
            raise ValueError("Truncated image segment header")
        size = int.from_bytes(data[pos+4:pos+8], "little")
        pos += 8
        if pos + size > len(data):
            raise ValueError("Truncated image segment")
        for byte in data[pos:pos+size]:
            checksum ^= byte
        pos += size
    checksum_pos = pos | 15
    if checksum_pos + 33 != len(data) or data[checksum_pos] != checksum:
        raise ValueError("Invalid image checksum or length")
    if hashlib.sha256(data[:checksum_pos+1]).digest() != data[checksum_pos+1:]:
        raise ValueError("Invalid appended image digest")
    if data[32:36] != b"\x32\x54\xcd\xab":
        raise ValueError("Missing ESP application descriptor")
    normalized = bytearray(data[:checksum_pos])
    normalized[176:208] = bytes(32)  # app descriptor's ELF digest
    return bytes(normalized)

FQBN = ("esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=custom,PSRAM=opi,"
        "FlashMode=qio,FlashSize=8M,USBMode=hwcdc,UploadMode=default,"
        "UploadSpeed=921600,CPUFreq=240,DebugLevel=none,EraseFlash=none")

SKETCH = '''#include <Preferences.h>
#include <SD_MMC.h>
#include <FS.h>
#include <Update.h>
#include <Wire.h>
void setup() {}
void loop() {}
'''
PARTITIONS = '''nvs,data,nvs,0x9000,0x5000,
otadata,data,ota,0xe000,0x2000,
app0,app,ota_0,0x10000,0x250000,
app1,app,ota_1,0x260000,0x250000,
model,data,spiffs,0x4B0000,0x340000,
coredump,data,coredump,0x7F0000,0x10000,
'''

def relink(objects, output, libraries=None):
    objects = objects.resolve()
    manifest = load_manifest(objects)
    cli=shutil.which("arduino-cli")
    if not cli: raise ValueError("Install Arduino CLI and esp32:esp32@3.3.12 first")
    with tempfile.TemporaryDirectory(prefix="buddy-relink-") as td:
        tmp=Path(td);sketch=tmp/"BuddyRelink";sketch.mkdir()
        (sketch/"BuddyRelink.ino").write_text(SKETCH)
        (sketch/"partitions.csv").write_text(PARTITIONS)
        build=tmp/"build"
        stamp=build/"buddy_build_time.h";build.mkdir(parents=True)
        stamp.write_text(f'#undef __DATE__\n#define __DATE__ "{manifest["build_date"]}"\n'
                         f'#undef __TIME__\n#define __TIME__ "{manifest["build_time"]}"\n')
        maps=" ".join(quoted_flag(f"-{kind}-prefix-map={source}={target}") for source,target in
                      ((build,"/build"),(Path.home(),"/home/builder"))
                      for kind in ("ffile","fmacro"))+" -include "+quoted_flag(stamp)
        probe=subprocess.run([cli,"compile","--fqbn",FQBN,"--build-path",str(build),
                              "--show-properties=expanded",str(sketch)],
                             stdout=subprocess.PIPE,stderr=subprocess.PIPE,text=True)
        if probe.returncode:
            raise ValueError("Cannot inspect the installed toolchain: "+probe.stderr[-4000:])
        props=dict(line.split("=",1) for line in probe.stdout.splitlines() if "=" in line)
        for key,version in (("runtime.platform.path","3.3.12"),
                            ("runtime.tools.esp32s3-libs.path","3.3.12"),
                            ("runtime.tools.esp-x32.path","2601")):
            if Path(props.get(key, "")).name != version:
                raise ValueError("Install the pinned Arduino-ESP32 3.3.12 / esp-x32 2601 toolchain")
        command=[cli,"compile","--verbose","--fqbn",FQBN,"--build-path",str(build),
                 "--build-property",f"compiler.c.extra_flags={maps}",
                 "--build-property",f"compiler.cpp.extra_flags={maps}"]
        if libraries: command += ["--libraries",str(libraries.resolve())]
        command.append(str(sketch))
        result=subprocess.run(command,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if result.returncode: raise ValueError(result.stdout[-8000:])
        link_line=next((line for line in result.stdout.splitlines()
                        if "-Wl,--Map=" in line and "BuddyRelink.ino.elf" in line),None)
        if not link_line: raise ValueError("Could not locate Arduino's final linker command")
        args=split_command(link_line)
        replacements=0; i=0
        while i<len(args):
            if args[i].endswith("BuddyRelink.ino.cpp.o"):
                args[i]=str(objects/"Buddy.ino.cpp.o");replacements+=1
                args.insert(i+1,str(objects/"buddy_esp32_sr.c.o"));i+=1
            elif args[i].startswith("-Wl,--Map="):
                args[i]="-Wl,--Map="+str(tmp/"Buddy-relinked.map")
            elif args[i]=="-o" and i+1<len(args) and args[i+1].endswith("BuddyRelink.ino.elf"):
                args[i+1]=str(tmp/"Buddy-relinked.elf")
            i+=1
        if replacements!=1: raise ValueError("Expected exactly one temporary sketch object in linker command")
        linked=subprocess.run(args,cwd=sketch,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        if linked.returncode: raise ValueError(linked.stdout[-8000:])
        convert_line=next((line for line in result.stdout.splitlines()
                           if "elf2image" in line and "BuddyRelink.ino.elf" in line),None)
        if not convert_line:
            raise ValueError("Could not locate Arduino's image converter command")
        convert=[arg.replace(str(build/"BuddyRelink.ino.elf"),str(tmp/"Buddy-relinked.elf"))
                    .replace(str(build/"BuddyRelink.ino.bin"),str(tmp/"Buddy-relinked.bin"))
                 for arg in split_command(convert_line)]
        subprocess.run(convert,check=True,stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True)
        image=(tmp/"Buddy-relinked.bin").read_bytes()
        image_payload(image)
        if len(image)>0x250000 or f'BUDDY:WS154V2-BW:{manifest["release"]}UL'.encode() not in image:
            raise ValueError("Relinked image exceeds the app partition or has a wrong release marker")
        output=output.resolve();binary=output.with_suffix(".bin")
        if output==binary or output in [objects/n for n in OBJECTS] or binary in [objects/n for n in OBJECTS]:
            raise ValueError("Output must be an ELF path distinct from release input objects")
        output.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(tmp/"Buddy-relinked.elf",output)
        shutil.copyfile(tmp/"Buddy-relinked.bin",binary)
        print(f"Relink succeeded: {output}\nValidated firmware image: {binary}\nNo board was touched.")
        return {"image_sha256":sha(binary),"build_log":result.stdout}


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("--objects",type=Path,required=True)
    p.add_argument("--output",type=Path,required=True,help="ELF output; a matching .bin is also generated")
    p.add_argument("--libraries",type=Path,help="Optional Arduino libraries directory")
    a=p.parse_args()
    try:
        relink(a.objects,a.output,a.libraries)
    except (OSError,ValueError,KeyError,TypeError,subprocess.CalledProcessError) as exc:
        p.exit(1,f"Relink stopped: {exc}\n")


if __name__=="__main__": main()
