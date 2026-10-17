#!/usr/bin/env python3
"""Compile and package the exact checked-in Buddy source. Never opens a USB port."""
import argparse
from datetime import datetime
import hashlib
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path
from relink_release import quoted_flag

ROOT = Path(__file__).resolve().parents[1]
FQBN = ("esp32:esp32:esp32s3:CDCOnBoot=cdc,PartitionScheme=custom,PSRAM=opi,"
        "FlashMode=qio,FlashSize=8M,USBMode=hwcdc,UploadMode=default,"
        "UploadSpeed=921600,CPUFreq=240,DebugLevel=none,EraseFlash=none")
def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()
def release_number():
    return int(re.search(r"^#define BUDDY_FIRMWARE_VERSION (\d+)UL$",
                         (ROOT/"firmware/Buddy/buddy_update_format.h").read_text(), re.M).group(1))
def checksums(folder):
    files=sorted(p for p in folder.rglob("*") if p.is_file() and p.name not in {"SHA256SUMS",".DS_Store"}
                 and "__pycache__" not in p.parts and p.suffix not in {".pyc",".pyo"})
    (folder/"SHA256SUMS").write_text("".join(f"{sha(p)}  {p.relative_to(folder).as_posix()}\n" for p in files))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-path",type=Path,default=ROOT/"build/esp32s3")
    args=parser.parse_args()
    build=args.build_path.resolve();build.mkdir(parents=True,exist_ok=True)
    built_at=datetime.now()
    months=("Jan","Feb","Mar","Apr","May","Jun","Jul","Aug","Sep","Oct","Nov","Dec")
    build_date=f"{months[built_at.month-1]} {built_at.day:2d} {built_at.year}"
    build_time=built_at.strftime("%H:%M:%S")
    timestamp_header=build.with_suffix(".build_time.h")
    timestamp_header.write_text(f'#undef __DATE__\n#define __DATE__ "{build_date}"\n'
                                f'#undef __TIME__\n#define __TIME__ "{build_time}"\n')
    cli=shutil.which("arduino-cli")
    if not cli: raise RuntimeError("Install Arduino CLI and esp32:esp32@3.3.12; see docs/DEVELOPMENT.md")
    common=[cli,"compile","--fqbn",FQBN,"--build-path",str(build)]
    result=subprocess.run([*common,"--show-properties=expanded",str(ROOT/"firmware/Buddy")],
                          check=True,text=True,stdout=subprocess.PIPE)
    props=dict(line.split("=",1) for line in result.stdout.splitlines() if "=" in line)
    core=Path(props["runtime.platform.path"])
    sdk=Path(props["runtime.tools.esp32s3-libs.path"])
    if core.name!="3.3.12" or sdk.name!="3.3.12" or Path(props.get("runtime.tools.esp-x32.path", "")).name!="2601":
        raise RuntimeError("This release requires Arduino-ESP32/esp32s3-libs 3.3.12 and esp-x32 2601")
    maps=" ".join(quoted_flag(f'-{flag}-prefix-map={path}={replacement}') for path,replacement in
                  [(ROOT,"/buddy"),(build,"/build"),(Path.home(),"/home/builder")]
                  for flag in ("ffile","fmacro"))+" -include "+quoted_flag(timestamp_header)
    subprocess.run([*common,"--clean","--build-property","compiler.c.extra_flags="+maps,
                    "--build-property","compiler.cpp.extra_flags="+maps,str(ROOT/"firmware/Buddy")],check=True)
    release=release_number()
    folder=ROOT/"releases"/str(release);images=folder/"first-install"
    images.mkdir(parents=True,exist_ok=True)
    esptool=shutil.which("esptool")
    if not esptool: raise RuntimeError("Install pinned installer/requirements.txt in a Python environment")
    subprocess.run([esptool,"--chip","esp32s3","elf2image","--flash-mode","dio",
                    "--flash-freq","80m","--flash-size","8MB","--output",
                    str(images/"bootloader.bin"),str(sdk/"bin/bootloader_dio_80m.elf")],check=True)
    for source,name in [(build/"Buddy.ino.bin","Buddy.ino.bin"),
                        (build/"Buddy.ino.partitions.bin","partitions.bin"),
                        (core/"tools/partitions/boot_app0.bin","boot_app0.bin"),
                        (sdk/"esp_sr/srmodels.bin","srmodels.bin")]:
        shutil.copyfile(source,images/name)
    entries=[(0,"bootloader.bin",0x8000),(0x8000,"partitions.bin",0x1000),
             (0xe000,"boot_app0.bin",0x2000),(0x10000,"Buddy.ino.bin",0x250000),
             (0x4b0000,"srmodels.bin",0x340000)]
    for _,name,limit in entries:
        data=(images/name).read_bytes()
        if not 0<len(data)<=limit: raise RuntimeError(f"{name} exceeds its real partition")
        if re.search(rb"/Users/[^/\x00]+/",data): raise RuntimeError(f"Private path embedded in {name}")
    manifest={"release":release,"target":"Waveshare ESP32-S3-ePaper-1.54 V2 monochrome (non-touch)",
              "flash_size":0x800000,"channel":"stable",
              "images":[{"address":hex(address),"file":name,"sha256":sha(images/name),"max_size":limit}
                        for address,name,limit in entries]}
    (folder/"manifest.json").write_text(json.dumps(manifest,indent=2)+"\n")
    subprocess.run([sys.executable,str(ROOT/"firmware/Buddy/package_update.py"),
                    str(images/"Buddy.ino.bin"),str(folder/"BUDDY.UPD")],check=True)
    shutil.copyfile(folder/"BUDDY.UPD",ROOT/"BUDDY.UPD")
    # LGPL 2.1 §6(a): retain the exact non-library application objects needed
    # to relink a modified LGPL library with this exact firmware release.
    relink=folder/"compliance"/"relink";relink.mkdir(parents=True,exist_ok=True)
    # A rebuilt image invalidates prior relink acceptance, even with the same ID.
    (relink/"relink-test.json").unlink(missing_ok=True)
    app_objects={"Buddy.ino.cpp.o":build/"sketch"/"Buddy.ino.cpp.o",
                 "buddy_esp32_sr.c.o":build/"sketch"/"buddy_esp32_sr.c.o"}
    object_manifest=[]
    for name,source in app_objects.items():
        if not source.is_file(): raise RuntimeError(f"Missing relink object: {source}")
        destination=relink/name;shutil.copyfile(source,destination)
        object_manifest.append({"file":name,"sha256":sha(destination),"bytes":destination.stat().st_size})
    relink_helper=ROOT/"tools"/"relink_release.py"
    if not relink_helper.is_file(): raise RuntimeError("Missing source-private relink helper")
    shutil.copyfile(relink_helper,relink/"relink.py")
    shutil.copyfile(ROOT/"tools/RELINK.md",relink/"RELINK.md")
    (relink/"object-manifest.json").write_text(json.dumps({
        "release":release,"application_sha256":sha(images/"Buddy.ino.bin"),
        "fqbn":FQBN,"arduino_core":"3.3.12","toolchain":"esp-x32 2601","build_date":build_date,
        "build_time":build_time,"objects":object_manifest
    },indent=2)+"\n")
    source_files=sorted(p for directory in ("firmware","third_party") for p in (ROOT/directory).rglob("*")
                        if p.is_file() and "__pycache__" not in p.parts and not p.name.endswith(".pyc"))
    (folder/"SOURCE-SHA256SUMS").write_text("".join(f"{sha(p)}  {p.relative_to(ROOT).as_posix()}\n"
                                                 for p in source_files))
    (folder/"build-info.json").write_text(json.dumps({
        "release":release,"channel":"stable","fqbn":FQBN,"arduino_core":"3.3.12",
        "sdk_versions":(sdk/"versions.txt").read_text(),"app_bytes":(images/"Buddy.ino.bin").stat().st_size,
        "hardware_tested":False,"build_command":"python3 tools/build_release.py --build-path build/esp32s3"
    },indent=2)+"\n")
    checksums(folder)
    print(f"Packaged {release}. No board was touched. Run tools/prepare_compliance.py, then tools/test_relink_release.py before exporting.")

if __name__=="__main__":
    try: main()
    except (OSError,RuntimeError,subprocess.CalledProcessError) as exc:
        print(f"Build stopped: {exc}",file=sys.stderr);raise SystemExit(1)
