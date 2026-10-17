"""Transform compliance transport packaging for a single GitHub browser upload."""
import hashlib
import json
from pathlib import Path
import shutil
import zipfile

SOURCE="arduino-esp32-3.3.12-source.tar.gz"
PART_BYTES=20*1024*1024


def package_compliance(compliance, helper):
    compliance=Path(compliance)
    source=compliance/SOURCE
    data=source.read_bytes()
    parts=[]
    for offset in range(0,len(data),PART_BYTES):
        name=SOURCE+f".part{len(parts)+1:02d}"
        chunk=data[offset:offset+PART_BYTES]
        (compliance/name).write_bytes(chunk)
        parts.append({"file":name,"bytes":len(chunk),"sha256":hashlib.sha256(chunk).hexdigest()})
    manifest={"schema_version":1,"file":SOURCE,"bytes":len(data),
              "sha256":hashlib.sha256(data).hexdigest(),"parts":parts}
    (compliance/"source-parts.json").write_text(json.dumps(manifest,indent=2)+"\n")
    shutil.copy2(helper,compliance/"assemble_source.py")
    source.unlink()
    notices=compliance/"notices"
    notice_index={}
    with zipfile.ZipFile(compliance/"notices.zip","w",compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(notices.rglob("*")):
            if path.is_file():
                name=path.relative_to(compliance).as_posix()
                archive.write(path,name)
                notice_index[name]=hashlib.sha256(path.read_bytes()).hexdigest()
    (compliance/"notices-index.json").write_text(json.dumps(notice_index,indent=2)+"\n")
    shutil.rmtree(notices)


def verify_compliance(compliance):
    compliance=Path(compliance)
    manifest=json.loads((compliance/"source-parts.json").read_text())
    inventory=json.loads((compliance/"dependency-inventory.json").read_text())
    core=next(item for item in inventory["sources"] if item["name"]=="arduino-esp32")
    if manifest["file"]!=SOURCE or manifest["schema_version"]!=1:
        raise ValueError("Invalid source parts manifest")
    digest=hashlib.sha256();total=0
    if not manifest["parts"]: raise ValueError("Missing source parts")
    expected_parts=set()
    for i,part in enumerate(manifest["parts"],1):
        name=SOURCE+f".part{i:02d}"
        if part["file"]!=name: raise ValueError("Invalid source part ordering")
        data=(compliance/name).read_bytes();expected_parts.add(name)
        if len(data)!=part["bytes"] or len(data)>PART_BYTES or hashlib.sha256(data).hexdigest()!=part["sha256"]:
            raise ValueError("Source part checksum mismatch: "+name)
        digest.update(data);total+=len(data)
    if {p.name for p in compliance.glob(SOURCE+".part*")}!=expected_parts:
        raise ValueError("Unexpected source part files")
    if total!=manifest["bytes"] or digest.hexdigest()!=manifest["sha256"] or digest.hexdigest()!=core["archive_sha256"]:
        raise ValueError("Full corresponding source checksum mismatch")
    expected={"notices/"+item["name"]+"/"+name for item in inventory["sources"] for name in item["notices"]}
    notice_index=json.loads((compliance/"notices-index.json").read_text())
    with zipfile.ZipFile(compliance/"notices.zip") as archive:
        if (not expected.issubset(notice_index) or set(archive.namelist())!=set(notice_index)
                or len(archive.namelist())!=len(notice_index) or archive.testzip()):
            raise ValueError("Incomplete third-party notice archive")
        for name in archive.namelist():
            if not name.startswith("notices/") or Path(name).is_absolute() or ".." in Path(name).parts:
                raise ValueError("Unsafe notice path")
            if hashlib.sha256(archive.read(name)).hexdigest()!=notice_index[name]:
                raise ValueError("Changed third-party notice: "+name)
    return manifest
