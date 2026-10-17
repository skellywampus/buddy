#!/usr/bin/env python3
"""Verify and export the public repository, excluding private worktree history/data."""
import argparse
from datetime import datetime
from pathlib import Path
import shutil
import zipfile
from build_release import ROOT, release_number
from verify_release import verify
from verify_relink import verify_relink
from verify_public_export import verify_public_export
from browser_package import package_compliance
from build_release import checksums

DIRECTORIES={".github","docs","firmware","installer","releases","LICENSES","third_party","tools"}
IGNORED={"__pycache__",".DS_Store",".git",".venv","build","backups"}
ROOT_NAMES={"LICENSE","INSTALLER-TOOLS-LICENSE",".gitignore",".gitattributes","BUDDY.UPD",
            "README.md","CHANGELOG.md","CONTRIBUTING.md","FIRST-FLASH.md","FIRST-INSTALL.md",
            "LICENSE-SCOPE.md","NOTICE.md","SECURITY.md","THIRD_PARTY.md","UPDATE.md","USB-UPDATE.md"}
ROOT_NAMES.update(prefix+suffix for prefix in ("FLASH-NEW-BUDDY","USB-UPDATE-BUDDY","USB-RESTORE-PREVIOUS")
                  for suffix in (".bat",".command",".sh"))
def selected_files():
    for path in sorted(ROOT.rglob("*")):
        if not path.is_file(): continue
        relative=path.relative_to(ROOT)
        if any(part in IGNORED for part in relative.parts): continue
        if len(relative.parts)>1 and relative.parts[0]=="releases" and relative.parts[1]!=str(release_number()): continue
        if relative.name=="SOURCE-SHA256SUMS": continue
        if path.suffix in {".pyc",".pyo",".partial",".log"}: continue
        if len(relative.parts)==1:
            if path.name not in ROOT_NAMES: continue
        elif relative.parts[0] not in DIRECTORIES: continue
        if path.is_symlink(): raise RuntimeError("Cannot export a symbolic link: "+str(relative))
        if path.stat().st_size>=100*1024*1024:
            raise RuntimeError(f"File exceeds GitHub's normal Git limit: {relative}")
        yield path,relative
def verify_release_compliance():
    verify_relink(ROOT/"releases"/str(release_number()),release_number())
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,default=ROOT/"build/publication")
    args=parser.parse_args()
    verify()
    verify_release_compliance()
    container=args.output.resolve()/("Buddy-"+str(release_number())+"-"+datetime.now().strftime("%Y%m%d-%H%M%S"))
    container.mkdir(parents=True,exist_ok=False)
    destination=container/"Buddy.pending"
    for source,relative in selected_files():
        target=destination/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(source,target)
    public_release=destination/"releases"/str(release_number())
    # Keep exact corresponding source and every notice, but transport them as
    # browser-sized source pieces and one notice ZIP. Buddy binaries and the
    # bound relink evidence remain unchanged.
    package_compliance(public_release/"compliance",ROOT/"tools/assemble_source.py")
    checksums(public_release)
    verify_public_export(destination,release_number())
    verified=container/"Buddy"
    destination.rename(verified)
    destination=verified
    archive=container/f"Buddy-GitHub-{release_number()}.zip"
    with zipfile.ZipFile(archive,"w",compression=zipfile.ZIP_DEFLATED,compresslevel=6) as output:
        for path in sorted(destination.rglob("*")):
            if path.is_file(): output.write(path,"Buddy/"+path.relative_to(destination).as_posix())
    print(f"Public repository: {destination}\nComplete release ZIP: {archive}")
    print("Buddy source and tests are included; private Git history, caches, and device backups are excluded. No board was flashed.")
if __name__=="__main__": main()
