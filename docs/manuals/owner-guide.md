[Download the Word edition](Buddy%20Owner%20Guide.docx)

BUDDY  |  OCTOBER 2026

# Buddy Owner Guide

Everyday operation care updates and recovery

For adults who are comfortable using electronic devices but do not need firmware-development knowledge.

Current for production firmware release 2026101701

Buddy is a self-directed artificial-life companion on a small black-and-white e-paper device. He continues living without daily chores from you. This guide explains what he does, how to operate him safely, and how to update or recover him without erasing his life.

The most important rules are simple: use the BOOT button for normal interaction, use RESET/PWR to wake him, put him into GOODNIGHT before removing a microSD card, and never connect Cable Friends to a computer or charger.

## What Buddy is

Buddy is not a notification display, phone accessory, conversational assistant, or traditional virtual pet that waits for feeding. He has needs, temperament, learned preferences, habits, projects, possessions, a home, memories, and a family line. Human attention influences him, but his life does not stop when nobody presses the button.

A life normally lasts from 305 to 425 real days, averaging about one year. Buddy grows through baby, child, young, adult, and elder stages. When he dies of old age, a headstone memorial appears. A new generation begins after the memorial has been shown and at least one day has passed. The next Buddy starts as a new individual while inheriting a few family features, tendencies, possessions, structures, or traditions.

![Example of the memorial between generations](../assets/memorial.png)

Example of the memorial between generations

## Quick start

- Home screen: click BOOT once to interact with Buddy.
- Open About: double-click BOOT on the home screen.
- Move through About and Settings: click BOOT once.
- Return home from About or Settings: double-click BOOT.
- Change a Settings item: highlight it and hold BOOT for about one second.
- Manual sleep: hold BOOT on the home screen until GOODNIGHT appears.
- Wake: press or hold RESET/PWR. Voice cannot wake Buddy.

## The first start on a new board

A newly installed board opens a mandatory welcome sequence. You choose the first Buddy’s boy or girl appearance, first keepsake, and starting spark. One click moves forward, a double-click moves back, and a hold selects. These choices influence the starting point; they do not lock Buddy into a fixed personality. An update to an existing Buddy skips welcome and retains the saved life.

## The three screens

| Screen | What it shows | How to use it |
| --- | --- | --- |
| Buddy | His current activity or feeling, generation, age in days, and battery level. | One click interacts. Two clicks open About. Hold starts GOODNIGHT. |
| About | A compact description of his current life, condition, world, and temperature in Fahrenheit. | One click opens Settings. Two clicks return home. |
| Settings | Sound, Cable Friends, and Orientation. | One click advances. Hold changes the selected item. Two clicks return home. |

About and Settings return to the home screen after about 45 seconds. The firmware version is intentionally not shown there; developers can read it through USB diagnostics.

## How his life works

Buddy repeatedly evaluates his energy, comfort, security, curiosity, recent experiences, age, temperament, attachments, routines, possessions, home, and environment. Those conditions influence what he chooses to do. The action then changes his needs, memories, skills, attachments, or world. The visible activity is tied to that simulation rather than selected as an unrelated animation.

Visible moments change periodically while he is awake. Longer systems progress on their own timescales. A routine can form when he repeats meaningful actions at similar times. Personal projects take days. Ambitions and house construction can take weeks or months. The user does not accelerate them by repeatedly clicking.

## His world

Buddy may discover eight anchors: a shiny stone, little plant, quiet nook, homemade toy, wild grove, flower patch, story journal, and little instrument. Their appearance varies between lives. He can enjoy nature, smell flowers, make music, write, study, build, repair, play, explore, rest, and perform many other activities. Any current solo activity can also become a shared Cable Friends activity.

Sustained interests can become projects or long ambitions. A completed ambition may leave a family work such as a map tablet, garden trellis, lantern, or windwheel. These works can survive into later generations, weather, receive care, or disappear. Buddy may also build a house after he is old enough and has the needed experience. It can shelter later generations, wear down, collapse, and eventually be replaced.

## Individuality and appearance

Each life has a deterministic seed that gives it a stable appearance, lifespan, starting conditions, and variation in world objects. Age, inherited features, sex appearance, mood, activity, possessions, house, and family works all influence the pixel-art scene. Restarting does not reroll the current Buddy.

## Temperature

The onboard sensor measures the air near the board, not a calibrated room average. Pockets, hands, sunlight, and charging can warm it. Buddy uses a broad comfort model so ordinary pocket warmth is not treated as immediate danger. About shows the current reading in Fahrenheit when a valid sample is available.

## Sound and microphone

Sound has OFF, LOW, and HIGH settings. Buddy selects from eight bundled sound clips for interactions, milestones, sleep, wake, and independent moments. A music activity creates a five-clip tune with overlapping playback. LOW reduces decoded volume; HIGH uses the normal level.

When Sound is OFF, the shared speaker and microphone power rail is off. When Sound is LOW or HIGH, a BOOT gesture opens a local listening window for about 60 seconds. The microphone and speech engine remain paused or powered down outside that window to reduce battery use.

## Voice commands

Buddy recognizes two complete phrases while awake and listening:

- Buddy, Sound Off
- Buddy, Go To Sleep

Say the complete phrase, including “Buddy.” Sound must be restored from Settings because the microphone is physically off after Sound Off. Voice processing is local. Buddy does not record conversations, send audio to a phone or cloud service, understand open-ended speech, or wake when called. He may also show a simple listening response after sustained speech-like sound without recognizing a command.

## Sleep and low power

Normal operation uses invisible light sleep between deadlines. Buddy’s face remains on the e-paper display, and a BOOT press wakes the electronics for interaction. The retired hourly doze mode is no longer part of normal life.

Manual GOODNIGHT is different. Hold BOOT on the home screen. Buddy saves, closes optional storage, powers down the microphone, and enters indefinite deep sleep after drawing the sleeping face. The image remains because e-paper needs no power to hold it. Wake with RESET/PWR. Manual sleep is not a mechanical power disconnect, but it is the deliberate software-safe state for a microSD swap.

Three confirmed battery readings at or below approximately 3.50 volts trigger a silent low-battery shutdown after saving. Charge Buddy and use RESET/PWR to return. Do not rely on this software threshold as the battery’s only electrical protection.

## Orientation

Orientation can be AUTO, NORMAL, or FLIPPED. AUTO rotates while Cable Friends is active or a USB computer is detected. A conservative battery-voltage trend can also infer charge-only power, but this board has no dedicated charger-status signal. Three uninterrupted rising readings are required and the estimate expires. If a particular charger causes an unwanted direction, choose NORMAL or FLIPPED manually.

## Cable Friends

Cable Friends is a private, direct link between two compatible Buddies using an ordinary USB-C-to-USB-C cable. It deliberately repurposes one USB data conductor instead of using Bluetooth, Wi-Fi, or standard USB communication. Both Buddies show a full-screen greeting, render their own current portraits side by side, choose a shared activity, and remember encounters. A first meeting says A STRANGER; a remembered meeting says I KNOW YOU.

![Example of two Buddies sharing an activity](../assets/cable-friends.png)

Example of two Buddies sharing an activity

## Cable Friends safety

Use Cable Friends only between two awake, battery-powered V2 Buddies that have this firmware. Do not attach a computer, charger, hub, ordinary USB device, or powered-off Buddy while Cable Friends is enabled. The hardware cannot reliably detect every passive cable or charge-only power source after the pins are repurposed.

## Starting and ending a meeting

1. On both Buddies, open About, move to Settings, and highlight Cable Friends.
2. Hold BOOT on each Buddy to enable it. Both should show WAITING FOR BUDDY.
3. Only after both are waiting, connect them directly with the USB-C cable.
4. Let the greeting and shared activity appear on both screens.
5. Unplug the cable to finish. Buddy recognizes peer loss after about five seconds, shows a short farewell, restarts the normal USB hardware, and returns to ordinary life.

One activation permits one meeting. If no valid Buddy connects within two minutes, the waiting mode ends automatically. Holding BOOT while Cable Friends is showing cancels it. A new meeting requires enabling the mode again on both devices.

## What a meeting changes

A valid meeting can affect familiarity and each Buddy’s needs, while repeated meetings within the reward cooldown still show both Buddies without repeatedly awarding the same benefits. Encounter memory remains internal. The link does not exchange a complete life save and cannot clone one Buddy onto another.

## The optional microSD card

Buddy’s active life is stored inside the board. A microSD card is optional. When present, it can receive daily records, identity data, portraits, and memorial archives, and it can deliver a packaged firmware update. Removing the card does not erase Buddy, and copying the card does not clone his active life.

## Removing or inserting the card safely

1. Hold BOOT on the home screen until GOODNIGHT finishes drawing.
2. If a card was mounted, wait for SD SAFE TO SWAP. The message is intentionally absent when no card was present.
3. Remove the card. Eject it properly from a computer before reinserting it.
4. Wake Buddy with RESET/PWR after the card is back in place.

Do not remove or insert a card while Buddy is awake, still entering sleep, or installing an update. GOODNIGHT unmounts the filesystem before displaying the safe message; it does not electrically disconnect the card slot.

## Firmware updates

| Situation | Use | What it preserves |
| --- | --- | --- |
| Brand-new supported board | Expert ESP32 USB provisioning | Installs the complete matched first-flash image set, layout, app, and speech model. |
| Existing Buddy | BUDDY.UPD at the microSD card root | Installs through the inactive app slot. Keeps the life, history, and settings. |

First installation is for someone who already knows how to flash an ESP32 safely; it is not an end-user update procedure. Routine owner updates use the microSD package below. Use only files for the Waveshare ESP32-S3-ePaper-1.54 V2 black-and-white non-touch board. Never erase flash as routine troubleshooting, and never disconnect power during an installation. The current package is release 2026101701.

## Updating from microSD

1. Enter GOODNIGHT and wait for SD SAFE TO SWAP.
2. Move the card to a computer and copy BUDDY.UPD to the top level, replacing only an older update file.
3. Eject the card, return it to Buddy, and press RESET/PWR.
4. Keep power and the card connected through CHECKING FILE, INSTALLING, and UPDATE COMPLETE.

The same installed package may remain on the card; Buddy ignores an equal or older release. A damaged or wrong-target package is rejected. If an installation is interrupted, the prior app slot remains available; restart with the card inserted and try again.

## Troubleshooting

| Symptom | What it means | What to do |
| --- | --- | --- |
| No response | Buddy may be in manual or low-battery sleep. | Charge if needed, then use RESET/PWR. |
| Voice ignored | He may not be listening, Sound may be OFF, or the phrase was incomplete. | Wake/interact once, wait briefly, then say a complete supported phrase. Restore sound in Settings if OFF. |
| CLOCK AGING PAUSED | The real-time clock is not trusted, so biological aging has stopped to protect the save. | Keep Buddy powered and allow automatic retries. Do not erase or factory-reset him. |
| Cable Friends waiting | No valid peer has connected yet. | Enable the mode on the other Buddy and connect them, or wait two minutes/cancel with a hold. |
| Screen flashes | The e-paper is cleaning old pixels. | This is normal. Full refreshes prevent accumulated ghosting. |
| Temperature unavailable | The sensor has not returned a valid sample yet. | Wait; Buddy retries automatically. |
| Controls appear stuck | A task may have failed or the system may need a controlled restart. | Click BOOT five times quickly. This preserves the life and is not a factory reset. |

## Useful terms

E-paper means the screen holds an image without continuous display power and may flash while refreshing. Firmware is the software installed inside Buddy. NVS is the protected internal flash area that stores the active life and preferences. RTC is the battery-backed real-time clock used for age and timestamps. OTA slots are two firmware spaces that allow an update to be written without immediately overwriting the running version.

## Limits and care

- Buddy is not waterproof and is not designed for rough handling.
- Do not press on or bend the e-paper panel.
- Supervise charging, card handling, firmware installation, and Cable Friends when children are using Buddy.
- The microphone provides local command recognition only; Buddy is not a general speech assistant.
- Bluetooth and Wi-Fi are not used by the production firmware.

This guide describes production release 2026101701. The project README explains the supported hardware, firmware behavior, release files, and third-party licensing. Buddy's source is available under the MIT License, while third-party components retain their own terms.

