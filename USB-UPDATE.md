# Update an existing Buddy over USB

This folder contains release **2026101701**. It preserves the existing display and simulation behavior and adds source-private LGPL relink materials; no board was flashed for the licensing relink test.

## Update

1. Keep the complete folder together. Close serial monitors and turn Cable Friends off.
2. Connect one Buddy to the computer with a USB-C data cable and wake him.
3. On Mac, open Terminal in the Buddy folder and run `bash USB-UPDATE-BUDDY.command` (or double-click it if executable permission is already set). GitHub browser uploads do not preserve executable permission. On Windows, run **USB-UPDATE-BUDDY.bat**. On Linux, run `bash USB-UPDATE-BUDDY.sh`.
4. The launcher sets up pinned flashing dependencies if needed, detects the board, validates the package and partition layout, and saves a full 8 MB recovery backup. It then writes and verifies the inactive app slot.
5. Leave the cable connected until the tool confirms that release **2026101701** is running after restart.

Python 3.10+ is required; the first dependency setup requires internet. Backups and the per-board previous-firmware receipt are saved outside the downloaded folder: `~/Library/Application Support/Buddy/backups` on Mac, `%LOCALAPPDATA%\Buddy\backups` on Windows, or `$XDG_DATA_HOME/Buddy/backups` / `~/.local/share/Buddy/backups` on Linux. Keep that private folder. The tool prints each backup's full path. Life, world, settings, model, partition table and bootloader are not rewritten. The existing application slot is retained.

You can check the local files without connecting a board:
`python3 installer/launch_usb_update.py --verify-files`

## Return to the previous firmware

Reconnect the same Buddy. On Mac run `bash USB-RESTORE-PREVIOUS.command` from Terminal in the Buddy folder; use the corresponding .bat or .sh on Windows/Linux. The tool makes another full backup, verifies that the preserved previous app still matches the per-board receipt, and switches only the OTA selector. It waits for the previous app to boot.

This keeps the Buddy's **current** life and history, including time spent on the newer firmware. It does not rewind his saved data. The receipt is specific to that board and must remain in the private backup folder. Receipts in an older download's `build/backups` are still accepted as a fallback. Restoration refuses to proceed if another update has replaced the old slot.

Do not use FLASH-NEW-BUDDY on an existing life. That launcher deliberately refuses recognized existing Buddies. Routine updates can also use microSD `BUDDY.UPD`.
