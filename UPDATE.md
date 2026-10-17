# Update Buddy from a microSD card

This is the supported owner update procedure. For a brand-new board that has never run Buddy, an experienced ESP32 technician must first install the complete release image set over USB; see [FIRST-INSTALL.md](FIRST-INSTALL.md). This page covers later updates only.

The ready-to-copy update file is [BUDDY.UPD](BUDDY.UPD) in this folder. You do not need Arduino, a terminal, or a USB connection for future releases.

1. Charge Buddy, then hold BOOT until his **GOODNIGHT / SD SAFE TO SWAP** face has fully appeared. Remove his microSD card and insert it into your computer. Buddy closes files and unmounts the card before showing that line.
2. Copy **BUDDY.UPD** directly onto the card (not into the `buddy` folder). If an older `BUDDY.UPD` is already there, replace that update file. Leave Buddy's other files alone.
3. Eject the card safely and put it back in Buddy.
4. Press RESET/PWR to restart him. Watch for **CHECKING FILE** and **INSTALLING**. Keep the card inserted and Buddy powered until he restarts and shows **UPDATE COMPLETE**.
5. Buddy will return to his usual screen with the same life and history. The card can stay in for archives; an installed update file is ignored on later boots.

If he says the file is damaged or for the wrong board, copy a fresh `BUDDY.UPD` for the **Waveshare ESP32-S3-ePaper-1.54 V2 black-and-white** board. If he says to charge first, charge him and restart. If the update is interrupted, restart with the card still inserted; his previous firmware remains available during installation. Do not erase the card's `buddy` folder or the board's flash.

This card method works after the one-time USB provisioning. Offline voice recognition also needs the speech model installed during that first setup; `BUDDY.UPD` updates the application, not that model. Bluetooth discovery is retired; Cable Friends is manually enabled through Settings. This folder carries release `2026101701`; the [USB updater and previous-app restoration tool](USB-UPDATE.md) are also available. Update both Buddies to the same compatible release before using Cable Friends together. About intentionally omits developer version information. Temperature failures retry every two seconds initially, backing off to 30 seconds and then two minutes if persistent.

The sleeping face is a software-safe card handoff, not a physical power-off. Automatic hourly dozing has been retired: Buddy remains normally active, and SD is safe to remove only after deliberate manual sleep (or critical-battery shutdown) finishes showing the safe-to-swap face. Do not remove a card while Buddy is awake or displaying CHECKING FILE / INSTALLING, and use your computer's eject command before returning the card.
