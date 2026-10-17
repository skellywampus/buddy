#!/usr/bin/env python3
"""Create version-specific source, notices and linked archive inventory for a release."""
import argparse
import concurrent.futures
import hashlib
import json
import re
import shutil
import tarfile
import urllib.request
from pathlib import Path
from build_release import ROOT, FQBN, release_number, sha, checksums
import subprocess

CACHE=ROOT/"build/compliance-cache"
def fetch(url,path):
    if path.is_file(): return path.read_bytes()
    if shutil.which("curl"):
        data=subprocess.run(["curl","--fail","--location","--silent","--show-error",
                             "--retry","3","--max-time","120",url],
                            check=True,stdout=subprocess.PIPE).stdout
    else:
        request=urllib.request.Request(url,headers={"User-Agent":"Buddy-release-provenance/1"})
        with urllib.request.urlopen(request,timeout=90) as response: data=response.read()
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_bytes(data)
    return data

def notice_name(path):
    return bool(re.match(r"(?i)^(license|licence|copying|copyright|notice)([._-]|$)",Path(path).name))

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-path",type=Path,default=ROOT/"build/esp32s3")
    parser.add_argument("--reuse-notices",action="store_true",help="Reuse the current release's pinned source/notices offline")
    args=parser.parse_args();build=args.build_path.resolve()
    folder=ROOT/"releases"/str(release_number());out=folder/"compliance"
    if sha(build/"Buddy.ino.bin") != sha(folder/"first-install/Buddy.ino.bin"):
        raise RuntimeError("Build image does not match the release; refusing an unrelated dependency inventory")
    out.mkdir(parents=True,exist_ok=True)
    sources=json.loads((ROOT/"tools/dependencies.lock.json").read_text())["sources"]
    record=[]
    def source_notices(source):
        name,repo,commit=source["name"],source["repo"],source["commit"]
        target=out/"notices"/name
        notices=[]
        if "archive_sha256" in source:
            archive=CACHE/(name+".tar.gz")
            fetch(f"https://codeload.github.com/{repo}/tar.gz/{commit}",archive)
            if sha(archive)!=source["archive_sha256"]: raise RuntimeError(f"{name} source archive checksum mismatch")
            with tarfile.open(archive) as tar:
                for member in tar.getmembers():
                    path=Path(*Path(member.name).parts[1:])
                    if member.isfile() and (notice_name(path) or path.as_posix() in source.get("notice_files", [])):
                        destination=target/path
                        destination.parent.mkdir(parents=True,exist_ok=True)
                        destination.write_bytes(tar.extractfile(member).read())
                        notices.append(path.as_posix())
            if name=="arduino-esp32":
                shutil.copyfile(archive,out/"arduino-esp32-3.3.12-source.tar.gz")
        else:
            tree=json.loads(fetch(f"https://api.github.com/repos/{repo}/git/trees/{commit}?recursive=1",
                                  CACHE/(name+"-tree.json")))
            if tree.get("truncated"): raise RuntimeError(f"Incomplete notice inventory for {name}")
            paths=[x["path"] for x in tree.get("tree",[]) if x["type"]=="blob" and
                   (notice_name(x["path"]) or x["path"] in source.get("notice_files", []))]
            if source.get("subtree"):
                paths=[p for p in paths if "/" not in p or p.startswith(source["subtree"]+"/")]
            for path in paths:
                data=fetch(f"https://raw.githubusercontent.com/{repo}/{commit}/{path}",CACHE/"raw"/name/path)
                destination=target/path;destination.parent.mkdir(parents=True,exist_ok=True)
                destination.write_bytes(data);notices.append(path)
        if not notices: raise RuntimeError(f"No notices found for {name}; cannot complete release")
        print(f"Preserved {len(notices)} notices: {name}",flush=True)
        return {**source,"url":f"https://github.com/{repo}/tree/{commit}","notices":sorted(notices)}
    if args.reuse_notices:
        record=json.loads((out/"dependency-inventory.json").read_text())["sources"]
        if len(record)!=len(sources): raise RuntimeError("Incomplete pinned notice inventory")
        for source,entry in zip(sources,record):
            expected=dict(source,url=f"https://github.com/{source['repo']}/tree/{source['commit']}",notices=entry.get("notices"))
            if entry!=expected or not entry.get("notices"):
                raise RuntimeError("Pinned source/notice inventory mismatch")
            if any(not (out/"notices"/source["name"]/name).is_file() for name in entry["notices"]):
                raise RuntimeError("Missing cached component notice")
            if source["name"]=="arduino-esp32" and sha(out/"arduino-esp32-3.3.12-source.tar.gz")!=source["archive_sha256"]:
                raise RuntimeError("Corresponding Arduino source checksum mismatch")
    else:
        with concurrent.futures.ThreadPoolExecutor(max_workers=4) as workers:
            record=list(workers.map(source_notices,sources))

    props=subprocess.run(["arduino-cli","compile","--fqbn",FQBN,"--build-path",str(build),
                          "--show-properties=expanded",str(ROOT/"firmware/Buddy")],
                         check=True,text=True,stdout=subprocess.PIPE).stdout
    props=dict(line.split("=",1) for line in props.splitlines() if "=" in line)
    compiler=Path(props["compiler.path"]).parent
    toolchain=compiler/"share/licenses"
    for rel in ["gcc/COPYING3","gcc/COPYING.RUNTIME","gcc/COPYING3.LIB",
                "newlib/COPYING.NEWLIB","newlib/COPYING.LIBGLOSS"]:
        path=toolchain/rel
        if not path.is_file() and rel=="newlib/COPYING.LIBGLOSS":
            path=toolchain/"binutils/COPYING.LIBGLOSS"
        if not path.is_file(): raise RuntimeError(f"Missing runtime notice {rel}")
        dest=out/"notices/toolchain"/rel;dest.parent.mkdir(parents=True,exist_ok=True)
        shutil.copyfile(path,dest)
    sdk=Path(props["runtime.tools.esp32s3-libs.path"])
    xtensa=sdk/"include/xtensa/esp32s3/include/xtensa/config/core-isa.h"
    shutil.copyfile(xtensa,out/"notices/toolchain/Xtensa-core-isa.h.txt")

    # Archive members selected by the linker, including subsequently discarded
    # sections, form a conservative inventory rather than a list of every SDK archive.
    text=(build/"Buddy.ino.map").read_text().split("Discarded input sections")[0]
    archives=sorted(set(re.findall(r"(?m)^((?:/|[A-Za-z]:[/\\]).+?\.a)\(",text)))
    if not archives: raise RuntimeError("No linked archives found")
    inventory=[]
    for path in map(Path,archives):
        inventory.append({"file":path.name,"sha256":sha(path)})
    library_sources=sorted(p.name for p in (build/"libraries").iterdir() if p.is_dir())
    (out/"dependency-inventory.json").write_text(json.dumps({
        "release":release_number(),"application_sha256":sha(folder/"first-install/Buddy.ino.bin"),
        "archives":inventory,"arduino_libraries":library_sources,
        "sources":record,"runtime_toolchain":"esp-x32 2601; GCC runtime exception and Newlib notices",
        "speech_model_sha256":sha(folder/"first-install/srmodels.bin")
    },indent=2)+"\n")
    checksums(folder)
    print(f"Complete: {len(inventory)} linked archives; bundled core source and pinned SDK notices. No board touched.")

if __name__=="__main__": main()
