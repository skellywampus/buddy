"""Verify the complete source-enabled public folder before publication."""
import hashlib
import json
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import urlsplit, unquote
from browser_package import verify_compliance
from verify_relink import verify_relink
from html.parser import HTMLParser
from zipfile import ZipFile
import xml.etree.ElementTree as ET
import posixpath


class LocalMedia(HTMLParser):
    def __init__(self): super().__init__();self.targets=[]
    def handle_starttag(self,tag,attributes):
        for key,value in attributes:
            if value and ((tag=="img" and key=="src") or (tag=="a" and key=="href")):
                self.targets.append(value)


def verify_manual(path):
    with ZipFile(path) as archive:
        if archive.testzip(): raise ValueError("Damaged Word manual: "+path.name)
        names=set(archive.namelist());images=0
        for name in names:
            if not name.endswith(".rels"): continue
            base=posixpath.dirname(posixpath.dirname(name))
            for relationship in ET.fromstring(archive.read(name)):
                if relationship.get("Type","").endswith("/image"):
                    images+=1
                    target=relationship.get("Target","")
                    local=posixpath.normpath(posixpath.join(base,target))
                    if relationship.get("TargetMode")=="External" or local not in names:
                        raise ValueError("Unembedded or missing Word image: "+path.name)
        if images<2: raise ValueError("Manual illustrations missing: "+path.name)


def verify_browser_limits(root):
    files=[p for p in root.rglob("*") if p.is_file()]
    if len(files)>500: raise ValueError(f"Public source export exceeds 500 files: {len(files)}")
    for path in files:
        if path.stat().st_size>25*1024*1024:
            raise ValueError("Browser upload exceeds 25 MiB per file: "+str(path.relative_to(root)))
        if path.name==".DS_Store" or "__pycache__" in path.parts or path.suffix in {".pyc",".partial",".log"}:
            raise ValueError("Generated runtime data in export: "+str(path.relative_to(root)))
    return files


def verify_public_export(root, release):
    root=root.resolve()
    files=verify_browser_limits(root)
    for name in ("build",".git"):
        if (root/name).exists(): raise ValueError("Private/generated content in export: "+name)
    for name in ("firmware/Buddy/Buddy.ino","firmware/Buddy/tests/host_test.cpp",
                 "firmware/Buddy/sounds/New-sound-1.wav",
                 "third_party/waveshare_epaper_display/PROVENANCE.md",
                 "tools/build_release.py","installer/tests/test_flash_new_buddy.py"):
        if not (root/name).is_file(): raise ValueError("Open-source project file missing: "+name)
    folder=root/"releases"/str(release)
    verify_relink(folder,release)
    verify_compliance(folder/"compliance")
    if {p.name for p in (root/"releases").iterdir()}!={str(release)}:
        raise ValueError("Public export contains an unrelated release")
    if any(p.is_symlink() for p in root.rglob("*")):
        raise ValueError("Public export contains a symbolic link")
    entries={}
    for line in (folder/"SHA256SUMS").read_text().splitlines():
        checksum,name=line.split(None,1);name=name.strip()
        path=(folder/name).resolve()
        if name in entries or not path.is_relative_to(folder) or not path.is_file():
            raise ValueError("Invalid exported checksum entry: "+name)
        if hashlib.sha256(path.read_bytes()).hexdigest()!=checksum:
            raise ValueError("Exported checksum mismatch: "+name)
        entries[name]=checksum
    actual={p.relative_to(folder).as_posix() for p in folder.rglob("*") if p.is_file() and p.name!="SHA256SUMS"}
    if set(entries)!=actual or (folder/"SOURCE-SHA256SUMS").exists():
        raise ValueError("Exported release checksum coverage is incomplete")
    for document in root.rglob("*.md"):
        if "notices" in document.relative_to(root).parts: continue
        content=document.read_text();html=LocalMedia();html.feed(content)
        for target in re.findall(r"!?\[[^\]]*\]\(([^)\n]+)\)",content)+html.targets:
            parsed=urlsplit(target.strip("<>"))
            if parsed.scheme or not parsed.path: continue
            local=(document.parent/unquote(parsed.path)).resolve()
            if not local.is_relative_to(root) or not local.exists():
                raise ValueError(f"Broken exported link in {document.relative_to(root)}: {target}")
    for path in (root/"docs/manuals").glob("*.docx"): verify_manual(path)
    commands=(("launch_first_flash.py",()),("launch_usb_update.py",()),
              ("launch_usb_update.py",("--restore-previous-app",)))
    for launcher,flags in commands:
        result=subprocess.run([sys.executable,str(root/"installer"/launcher),*flags,"--verify-files"],
                              cwd=root,env={k:v for k,v in os.environ.items() if k!="PYTHONDONTWRITEBYTECODE"},
                              stdout=subprocess.PIPE,stderr=subprocess.STDOUT,text=True,timeout=30)
        if result.returncode or "No board was touched" not in result.stdout:
            raise ValueError(f"Exported {launcher} failed offline validation:\n{result.stdout}")
    verify_browser_limits(root)
    git=shutil.which("git")
    if not git: raise ValueError("Git is required to verify the required relink files will be uploaded")
    with tempfile.TemporaryDirectory(prefix="buddy-git-export-") as td:
        repo=Path(td)
        subprocess.run([git,"init","--quiet",str(repo)],check=True)
        objects=[p.relative_to(root).as_posix() for p in (folder/"compliance/relink").glob("*.o")]
        result=subprocess.run([git,"--git-dir",str(repo/".git"),"--work-tree",str(root),
                               "-c","core.excludesFile="+str(repo/"empty-excludes"),
                               "check-ignore","--no-index",*objects],cwd=root,text=True,
                              stdout=subprocess.PIPE,stderr=subprocess.PIPE)
        if result.returncode!=1:
            raise ValueError("Required object files would be ignored by Git: "+result.stdout+result.stderr)
    print(f"PASS: MIT source export {release}: {len(files)} files, all <=25 MiB; firmware/tests/assets, offline USB install/update/restore, checksums, corresponding dependency source, notices, Markdown/HTML links, embedded manual images, and Git object inclusion")
