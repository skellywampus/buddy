#!/usr/bin/env bash
cd -- "$(dirname -- "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo 'Install Python 3.10+ using your distribution package manager, then run this again.'
  exit 1
fi
python3 installer/launch_usb_update.py --restore-previous-app "$@"
result=$?
if [[ -t 0 ]]; then
  printf '\nPress Return to close this window...'
  read -r reply
fi
exit "$result"
