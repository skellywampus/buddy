#!/bin/bash
cd -- "$(dirname -- "$0")" || exit 1
if ! command -v python3 >/dev/null 2>&1; then
  echo 'Install Python 3.10+ from https://www.python.org/downloads/ then run this again.'
  result=1
else
  python3 installer/launch_usb_update.py --restore-previous-app "$@"
  result=$?
fi
if [[ -t 0 ]]; then
  printf '\nPress Return to close this window...'
  read -r reply
fi
exit "$result"
