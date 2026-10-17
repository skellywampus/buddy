# SPDX-License-Identifier: MIT
"""Keep recovery data outside the publishable/downloaded application folder."""
import os
from pathlib import Path
import sys


def backup_directory():
    if sys.platform=="win32":
        base=Path(os.environ.get("LOCALAPPDATA") or Path.home()/"AppData/Local")
    elif sys.platform=="darwin":
        base=Path.home()/"Library/Application Support"
    else:
        value=os.environ.get("XDG_DATA_HOME","")
        base=Path(value) if value and Path(value).is_absolute() else Path.home()/".local/share"
    return base/"Buddy/backups"


def restore_receipt(root, backups, mac):
    current=backups/(mac+"-previous.json")
    legacy=root/"build/backups"/(mac+"-previous.json")
    # New receipts take priority; older downloads remain recoverable without
    # silently copying private files into the public tree.
    return current if current.exists() or not legacy.exists() else legacy
