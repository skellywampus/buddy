"""Set up a private Python environment, then run the backup-first installer."""
from pathlib import Path
import os
import subprocess
import sys
import venv

sys.dont_write_bytecode = True
os.environ["PYTHONDONTWRITEBYTECODE"] = "1"

HERE = Path(__file__).resolve().parent
REQUIREMENTS = ("esptool==5.3.0", "pyserial==3.5")


def cache_root():
    if os.name == "nt":
        base = Path(os.environ.get("LOCALAPPDATA", Path.home() / "AppData/Local"))
    elif sys.platform == "darwin":
        base = Path.home() / "Library/Caches"
    else:
        base = Path(os.environ.get("XDG_CACHE_HOME", Path.home() / ".cache"))
    return base / "Buddy/first-flash" / f"py{sys.version_info.major}{sys.version_info.minor}"


def environment_python(folder):
    return folder / ("Scripts/python.exe" if os.name == "nt" else "bin/python")


def dependencies_ready(python):
    if not python.is_file():
        return False
    probe = ("import importlib.metadata as m; "
             "assert m.version('esptool')=='5.3.0'; assert m.version('pyserial')=='3.5'")
    return subprocess.run([str(python), "-c", probe], stdout=subprocess.DEVNULL,
                          stderr=subprocess.DEVNULL, check=False).returncode == 0


def main(arguments=None, worker_name="flash_new_buddy.py"):
    arguments = sys.argv[1:] if arguments is None else arguments
    if sys.version_info < (3, 10):
        print("Buddy needs Python 3.10 or newer. Install it from https://www.python.org/downloads/")
        return 1
    worker = HERE / worker_name
    # Offline validation/help neither installs packages nor opens hardware.
    if "--verify-files" in arguments or "--help" in arguments or "-h" in arguments:
        return subprocess.run([sys.executable, str(worker), *arguments], check=False).returncode
    check = subprocess.run([sys.executable, str(worker), "--verify-files"], check=False)
    if check.returncode:
        return check.returncode
    folder = cache_root()
    python = environment_python(folder)
    if not dependencies_ready(python):
        print("Setting up Buddy's private flashing tools (internet needed on first run).",flush=True)
        print("Python packages only; no system Python packages or board firmware are changed during setup.",flush=True)
        folder.mkdir(parents=True, exist_ok=True)
        venv.EnvBuilder(with_pip=True).create(folder)
        subprocess.run([str(python), "-m", "pip", "install", "--disable-pip-version-check",
                        *REQUIREMENTS], check=True)
        if not dependencies_ready(python):
            raise RuntimeError("Flashing dependencies did not install correctly")
    return subprocess.run([str(python), "-u", str(worker), *arguments], check=False).returncode


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, RuntimeError, subprocess.CalledProcessError, KeyboardInterrupt) as exc:
        print(f"\nSetup stopped: {exc}", file=sys.stderr)
        print("No first-install write was started unless the flashing tool explicitly reached that stage.",file=sys.stderr)
        print("Check internet access and Python 3.10+. On Debian/Ubuntu, python3-venv may need installing.",file=sys.stderr)
        raise SystemExit(1)
