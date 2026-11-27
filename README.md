# Talko MEA8000 1.0

Speech and live formant synthesis for the Polaxis Talko 2 (RP2040). The encoder selects 13 speech banks with 258 selections and a continuous vowel synthesizer on bank 9.

[Download the 1.0 firmware for Talko 2](https://github.com/deladriere/Talko_MEA8000/releases/download/v1.0/Talko_MEA8000-1.0.uf2) · [Release files](https://github.com/deladriere/Talko_MEA8000/releases/tag/v1.0) · [Talko App Store](https://www.polaxis.be/talko-alternative-firmwares/) · [User manual](https://www.polaxis.be/talko-mea8000-user-manual/)

## Install

Connect Talko 2 by USB. Hold BOOTSEL, press and release RESET, then release BOOTSEL when the RPI-RP2 drive appears. Copy the UF2 to that drive. Talko restarts and says « un point zéro ».

## Play

Start on bank 0 with SOUND fully left, SPEED and PITCH at noon, and FX fully left. Press SPEAK to hear BONJOUR. Hold ERROR during the word and turn PITCH to play the captured frame as a note. In bank 9, hold SPEAK with the mode switch in the middle and sweep SOUND through the vowels.

After bank 9, the display shows C for the Citroën bank, E for Électronique, b for ZÉRO / UN and n for OUI / NON. The two-word banks switch near the middle of SOUND. USB serial sends the word when it starts and again on each repeat. Connect at 115200 baud. [Playing guide](docs/Manual.html) · [Complete vocabulary in knob order](docs/VOCABULARY.md)

## Build

Install PlatformIO Core, open this folder and run the build command.

~~~sh
pio run -e pico
~~~

The output is `.pio/build/pico/firmware.uf2`. The platform revision in `platformio.ini` selects Arduino-Pico 4.7.0. All speech frames used by the firmware are included in the source. [PlatformIO Raspberry Pi platform](https://github.com/maxgerhardt/platform-raspberrypi/tree/4d1ad09d78445f16688627de0819646fd655910e) · [Arduino-Pico source](https://github.com/earlephilhower/arduino-pico/tree/b8864711cd7801b7062c10f6b84d67f90284723b)

| Source | Purpose |
| --- | --- |
| `src/mea8000.cpp` and `src/mea8000.h` | Four-formant engine and frame decoder adapted from MAME |
| `src/banks.cpp` and `src/banks.h` | Frame bytes, starting pitches, labels, bank order and startup words |
| `src/audio.cpp` | Playback, effects, PWM output and USB serial |
| `src/main.cpp` | Panel, gate, encoder and display |
| `src/talko_startup.h` | Spoken version, display digits and rear LED sequence |

At startup, the voice says « un point zéro ». The display shows 1, the dot, then 0. The rear LED flashes once in red and once in blue.

## Credits and licensing

The vocabulary draws on Techni-Musique et Parole Informatique, Arno Jacobs’s MEA8000 collection, the Polaxis Emy/Mea tables, and the Citroën archive. A companion Citroën archive image credits Jacky Ruel for *ALARME 2.0* (1988).

The Polaxis code carries a scoped MIT license. The MEA8000 engine and Raspberry Pi helper have their own BSD notices. The historical speech data have separate provenance and unresolved redistribution terms. The companion framework-source ZIP contains the Arduino core sources and instructions for building with a modified core. [Project license](LICENSE) · [Source credits and license texts](THIRD_PARTY_NOTICES.md)
