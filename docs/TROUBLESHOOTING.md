# Troubleshooting

## The image is visible, but Buddy does not respond

E-paper retains an image without continuous power. Press PWR briefly to start after manual sleep, then release and try BOOT. Charge the battery and ensure the firmware matches the exact V2 board. If operation requires PWR to be held continuously, do not dismiss it as ghosting: it can indicate the battery latch is not being asserted. Update the affected 2026101303/04 application to 2026101305 using a powered, life-preserving update path.

## A USB flash appears successful, but nothing runs

Image verification and application execution are separate checks. Native USB/JTAG can leave ESP32-S3 in ROM download mode after a core-only reset. An expert can check the boot log and use Espressif's documented full-system restart. Do not erase the chip or reset a life to solve a boot-mode problem. Keep the complete backup and any error output.

## Clock / Aging Paused

Buddy does not invent elapsed time when RTC data is untrusted. Record the circumstances and allow normal clock recovery. Opening a menu may change the displayed frame but is not proof that a clock fault is fixed. A persistent fault needs diagnostics, not deleting saves or setting arbitrary dates. Normal time catch-up after sleep is expected.

## No voice response

Use Sound LOW/HIGH and press BOOT to open a listening window. Allow roughly 1–2 seconds for first-start recognition. Speak one of the two supported commands; unrestricted conversation is not supported. Sound OFF disables microphone power. Restore it in Settings rather than saying “Sound On.” The initial installation must include the matching speech model for command recognition.

## Cable Friends does not connect

Both units must be awake, compatible and manually in Cable Friends before connecting. Run them on their own batteries, never through a charger or computer. An activation times out after two minutes without a valid peer. After disconnection, enter the mode again for another meeting. Try a known suitable cable and record the firmware versions privately. Do not test by connecting the special mode to a USB host.

## An archive or update needs the SD card

Wait until GOODNIGHT completes before removing the card. Use a readable card, place BUDDY.UPD in the root for updates, and preserve the buddy archive directory. An internal eight-generation archive queue can fill without successful SD export; succession then pauses to protect history. Never delete or share a full-flash backup to work around an archive problem.

## Reporting a problem

Include board revision, firmware release, battery/USB conditions, whether a card is present, the exact action sequence, and a photograph if useful. Redact identifying data and do not upload live flash/NVS backups, full SD contents, personal paths or private audio. Five quick BOOT clicks request a non-destructive recovery restart; save erasure is not a recovery gesture.
