# ESP32-Darwinism

A series of interactive artificial-life games for an ESP32-S3 with a 1.28" round touchscreen.
Each game builds on the one before it: the rules, creatures and code carry forward, and each new game adds one big idea.

<!-- Add a GIF of the device here, e.g. <img src="media/hero.gif" width="320"> -->

## The games

| # | Game | The big idea | Folder |
|---|------|--------------|--------|
| 1 | **Pocket Ecosystem** | Hundreds of organisms on a grid evolve under predation, hazards and changing conditions | [`PocketEcosystem/`](PocketEcosystem/) |
| 2 | **Pocket Terrarium** | A persistent creature tank you care for daily, where genes shape visible bodies | [`PocketTerrarium/`](PocketTerrarium/) |
| 3 | *In development* | See [Roadmap](#roadmap) | |

### Game 1: Pocket Ecosystem

<!-- <img src="media/ecosystem.gif" width="240"> -->

A sandbox experiment. Herbivores search for food, avoid hazards and flee predators. Every organism carries four genes (speed, vision, reproduction threshold and caution) that mutate as they reproduce. You can feed the world, release predators, raise the temperature or change the mutation rate, then watch predator-prey cycles play out on a live population graph.

**[Rules, controls and experiments →](PocketEcosystem/)**

### Game 2: Pocket Terrarium

<!-- <img src="media/terrarium.gif" width="240"> -->

A tiny aquarium that depends on you. There's no automatic food: you feed the creatures, clean the water, and watch generations hatch, grow up and die. Genes control what each creature looks like, including body size, number of segments, legs, tail, eyes, colors and patterns, so a tank's lineage visibly changes over weeks. The tank saves itself to flash memory, so unplugging only pauses time.

**[Care guide, genes and controls →](PocketTerrarium/)**

## How the series evolves

| | Game 1 | Game 2 |
|---|---|---|
| **World** | 40×40 grid | Smooth, continuous movement |
| **Population** | Up to 300 tiny organisms | Up to 14 individual creatures |
| **Genes** | Numbers shown as color | Bodies you can see: legs, tails, patterns |
| **Time** | Fast, runs while you watch | Real time, saved between sessions |
| **Your role** | Experimenter | Caretaker |

## Hardware

- **[Waveshare ESP32-S3-Touch-LCD-1.28](https://www.waveshare.com/wiki/ESP32-S3-Touch-LCD-1.28)**
  - ESP32-S3 (dual-core, 240 MHz) with 2 MB PSRAM and 16 MB flash
  - 240×240 round LCD (GC9A01 driver)
  - Capacitive touch (CST816S)
  - Accelerometer and gyroscope (QMI8658, not used yet)
  - USB-C for power and programming

## Getting started

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. In **Boards Manager**, install **esp32 by Espressif Systems**.
3. In **Library Manager**, install **LovyanGFX** by lovyan03.
4. Select the board **ESP32S3 Dev Module**, then under **Tools** set:
   - Flash Size: **16MB (128Mb)**
   - PSRAM: **QSPI PSRAM**
   - USB CDC On Boot: **Enabled**
5. Open the `.ino` file in a game's folder, choose your board's port, and click **Upload**.

**Troubleshooting:** If the board doesn't appear as a port, try a different USB-C cable, since some only carry power. If the upload fails to connect, hold the **BOOT** button while plugging the board in, then upload again.

## Repository structure

```
ESP32-Darwinism/
├── README.md               ← you are here
├── PocketEcosystem/
│   ├── PocketEcosystem.ino
│   └── README.md
├── PocketTerrarium/
│   ├── PocketTerrarium.ino
│   └── README.md
└── media/                  ← GIFs and photos
```

Each sketch lives in a folder with the same name as its `.ino` file. The Arduino IDE requires this.

## Roadmap

- **Game 3: Make looks matter.** Predators that hunt by sight, in environments with different backgrounds, so camouflage evolves.
- **Game 4: Design vs. evolution.** Design a creature on a point budget and compete against a creature designed by an evolutionary AI running on the board.
- **Ideas:** Tilt the board to create currents (using the onboard IMU), day and night cycles, and a shared display-and-touch library for all the games.

## How this was built

The starter code was generated with an AI assistant (Claude). I ran it on the hardware, debugged it, tuned the rules, and directed each new feature and game.
