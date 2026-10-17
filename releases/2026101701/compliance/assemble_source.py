#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Reassemble the accompanying exact Arduino source without network access."""
import argparse
import hashlib
import json
from pathlib import Path
import tempfile


def assemble(folder, output=None):
    folder=Path(folder).resolve()
    manifest=json.loads((folder/"source-parts.json").read_text())
    name="arduino-esp32-3.3.12-source.tar.gz"
    if manifest.get("schema_version")!=1 or manifest.get("file")!=name:
        raise ValueError("Unsupported source-parts manifest")
    parts=manifest.get("parts",[])
    if not parts or len(parts)>20:
        raise ValueError("Invalid source part count")
    output=Path(output) if output else folder/name
    if output.exists():
        if output.is_file() and hashlib.sha256(output.read_bytes()).hexdigest()==manifest["sha256"]:
            return output
        raise ValueError("Output already exists with different contents; choose another --output")
    digest=hashlib.sha256();total=0
    temporary=None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent,prefix=".buddy-source-",delete=False) as stream:
            temporary=Path(stream.name)
            for i,part in enumerate(parts,1):
                expected=name+f".part{i:02d}"
                if part.get("file")!=expected:
                    raise ValueError("Unexpected source part name/order")
                path=folder/expected
                if path.is_symlink() or not path.is_file():
                    raise ValueError("Missing source part: "+expected)
                data=path.read_bytes()
                if len(data)!=part["bytes"] or hashlib.sha256(data).hexdigest()!=part["sha256"]:
                    raise ValueError("Damaged source part: "+expected)
                stream.write(data);digest.update(data);total+=len(data)
        if total!=manifest["bytes"] or digest.hexdigest()!=manifest["sha256"]:
            raise ValueError("Reassembled source checksum mismatch")
        # Exclusive creation avoids silently replacing an existing file.
        with output.open("xb") as stream, temporary.open("rb") as source:
            import shutil
            shutil.copyfileobj(source,stream)
        return output
    finally:
        if temporary is not None: temporary.unlink(missing_ok=True)


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,help="Optional destination .tar.gz path")
    args=parser.parse_args()
    try:
        output=assemble(Path(__file__).resolve().parent,args.output)
    except (OSError,ValueError,KeyError) as error:
        parser.exit(1,"Source assembly failed: "+str(error)+"\n")
    print("Verified complete corresponding source: "+str(output))


if __name__=="__main__": main()
