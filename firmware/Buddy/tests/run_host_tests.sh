#!/bin/bash
# No real hardware, flash, NVS or SD card is accessed by these tests.
set -euo pipefail
project_root="$(cd "$(dirname "$0")/../../.." && pwd)"
test_build="$(mktemp -d "${TMPDIR:-/tmp}/buddy-host-tests.XXXXXX")"
cd "$project_root"
test_compiler="${CXX:-clang++}"
export PYTHONDONTWRITEBYTECODE=1
export UBSAN_OPTIONS=halt_on_error=1
test_flags=(-std=c++17 -Wall -Wextra -fsanitize=address,undefined -fno-omit-frame-pointer -g)
"$test_compiler" "${test_flags[@]}" firmware/Buddy/tests/host_test.cpp -o "$test_build/production"
"$test_build/production"
"$test_compiler" "${test_flags[@]}" -Wno-unused-function -DBUDDY_ACCELERATED_LIFE_TEST firmware/Buddy/tests/host_test.cpp -o "$test_build/accelerated"
"$test_build/accelerated"
"$test_compiler" "${test_flags[@]}" -Wno-unused-function -DBUDDY_ACCELERATED_LIFE_TEST firmware/Buddy/tests/test_six_hour_clock.cpp -o "$test_build/clock"
"$test_build/clock"
for test_name in runtime_worker_test update_host_test climate_host_test power_host_test epaper_driver_test; do
  if [[ "$test_name" == power_host_test ]]; then
    "$test_compiler" "${test_flags[@]}" -Wno-unused-function "firmware/Buddy/tests/$test_name.cpp" -o "$test_build/$test_name"
  elif [[ "$test_name" == epaper_driver_test ]]; then
    "$test_compiler" "${test_flags[@]}" -I firmware/Buddy/tests/epaper_stubs "firmware/Buddy/tests/$test_name.cpp" -o "$test_build/$test_name"
  else
    "$test_compiler" "${test_flags[@]}" "firmware/Buddy/tests/$test_name.cpp" -o "$test_build/$test_name"
  fi
  "$test_build/$test_name"
done
python3 -m unittest discover -s firmware/Buddy/tests -p 'test_*.py'
python3 -m unittest discover -s installer/tests -p 'test_*.py'
python3 installer/launch_first_flash.py --verify-files
python3 tools/verify_release.py
printf 'All host and release checks passed. Test binaries: %s\n' "$test_build"
