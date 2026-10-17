"""Use the same pinned tools for the existing-life OTA-slot USB updater."""
import subprocess
import sys
sys.dont_write_bytecode = True
from launch_first_flash import main

if __name__ == "__main__":
    try:
        raise SystemExit(main(worker_name="update_buddy_usb.py"))
    except (OSError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(f"USB update stopped: {exc}", file=sys.stderr)
        raise SystemExit(1)
