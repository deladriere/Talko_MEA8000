# Talko MEA8000 1.0 — sources and rights

**Polaxis.** Jean-Luc Deladrière / Polaxis developed the Talko integration, panel mapping, real-time audio and musical controls. The project license covers Polaxis-owned material. [License and scope](LICENSE)

**MEA8000 synthesis.** The four-formant emulator derives from Antoine Miné’s 2006 MAME device. The adaptation replaces MAME’s register interface and timer with frame streams and RP2040 playback. Retain the per-file BSD-3-Clause copyright, conditions and disclaimer with source and binary distributions. [Pinned MAME device](https://github.com/mamedev/mame/blob/bdf56c6ae80289f7c4f944cd847d8fbeef13b0fc/src/devices/sound/mea8000.cpp) · [License](licenses/MEA8000-BSD-3-Clause.txt)

**RP2040 and USB.** The WS2812 LED helper derives from Raspberry Pi (Trading) Ltd.’s Pico examples. The build uses Earle F. Philhower III and contributors’ Arduino-Pico 4.7.0, the Arduino team’s core API, Raspberry Pi’s Pico SDK and TinyUSB by hathach and contributors. Their notices accompany this release. Consult the pinned framework’s dependency credits for component-specific terms. [WS2812 example](https://github.com/raspberrypi/pico-examples/blob/master/pio/ws2812/ws2812.pio) · [Pinned Arduino-Pico credits](https://github.com/earlephilhower/arduino-pico/tree/b8864711cd7801b7062c10f6b84d67f90284723b#licensing-and-credits) · [License files](licenses)

## Speech frames

The voice tables trace back to 1980s microcomputer speech synthesis. Techni-Musique et Parole Informatique advertised its Amstrad vocabulary in 1986. Complete records for 148 of Talko’s 258 menu selections also occur on the preserved TMPI disk, including repeated entries across banks. [Contemporary TMPI advertisement](https://www.oric.org/ftp/ceo/ceomag/downloads/tilt/1986/Tilt-30ocr.pdf)

| Material in the build | Source credit and evidence |
| --- | --- |
| French and English material, including composed selections | Arno Jacobs (Arno65) assembled and transcribed historical MEA8000 listings and contributed edits. His repository identifies the collection and its origins. No express license for general redistribution was identified. [Original collection](https://github.com/Arno65/MEA8000) |
| Électronique | All 20 selected records match both the transcribed collection and the Techni-Musique et Parole Informatique (TMPI) Amstrad disk files. The producer’s manual describes conditions for industrial or commercial use of its vocal elements. [TMPI manual](https://manualzilla.com/doc/6508434/mode-d-emploi-du-synthetiseur-vocal-techni) |
| Emy syllables and phrases | Ported from the Polaxis Emy/Mea real-chip firmware’s Phon100 and nombres tables. No separate reuse grant for those historical speech frames was identified. All 76 bank-7 records match the TMPI disk’s PHC.BIN and SAY.BIN byte for byte. The TMPI manual describes its PHON100 product. [Polaxis Mea](https://www.polaxis.be/mea-the-speech-synthesizer-with-a-vintage-french-talking-chip/) · [TMPI manual](https://manualzilla.com/doc/6508434/mode-d-emploi-du-synthetiseur-vocal-techni) |
| Citroën bank | The preserved Citroën archive supplies the pitch and speech frames. Image 3 contains the credit “ALARME VERSION 2.0 . COPYRIGHT JACKY RUEL 20/10/88”. Bank C draws on images 1 and 2 of that set. The credit identifies a provenance lead. Ownership of the individual voice records remains unverified. |
| Startup, Binaire and Oui / Non | The spoken version and the small banks also use historical records. In the answer bank, NON uses the French homophone originally labelled NOM. |

These credits identify the sources. The scope of any earlier permission for Polaxis to supply data with Mea vox remains unverified. The historic speech data are outside the project MIT license.

## Binary distribution

The Arduino-Pico core license includes the GNU LGPL 2.1. Distribution of a linked executable requires the notices and an applicable source/relink route. The project includes buildable application source and a pinned configuration. The companion Talko_MEA8000-1.0-framework-sources.zip supplies the core and ArduinoCore-API sources, RP2040 build files and instructions for recompiling and relinking the application with a modified core. Distribute those materials and the application source alongside the UF2. [LGPL 2.1, sections 4–6](https://www.gnu.org/licenses/old-licenses/lgpl-2.1.en.html)
