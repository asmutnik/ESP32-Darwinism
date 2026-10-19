# Game 2: Pocket Terrarium

<!-- <img src="../media/terrarium.gif" width="240"> -->

A tiny creature tank that depends on you. Feed it, keep the water clean, and watch generations hatch, grow up, lay eggs and die. Each creature's genes determine its body, so over a few weeks you can watch a lineage's appearance change.

## Caring for the tank

- **Feed it once a day.** Each tap drops 3 pellets. About 5 taps keeps 6 to 9 creatures healthy. Feed more and the population grows; feed less and it shrinks.
- **Watch for the warnings.** `Hungry!` means half the creatures are running low. Starving creatures **turn grey and slow down** to save energy.
- **Keep the water clean.** Uneaten food rots after a few hours, and eating and dying add waste. When `Dirty water - swipe!` appears, swipe to clean, or the creatures start losing energy. Overfeeding is a mistake too.
- **Neglect has consequences.** Miss one day and you'll lose some creatures, but the tank recovers. Miss three days and it dies.

## Life cycle

**Egg** (hatches in 3 hours) → **juvenile** (grows to full size over its first day and eats less) → **adult** (lays an egg about every 36 hours when well fed) → **old age** (lives 5–8 days).

Laying an egg costs the parent energy, so only well-fed creatures breed. The population ends up matching how much you feed it.

## Genes you can see

Every child inherits its parent's genes, usually with small mutations and occasionally with a big one.

| Gene | What you see | What it costs or does |
|------|--------------|-----------------------|
| **Size** | Small to large body | Bigger belly, but slower and hungrier |
| **Stretch** | Round blob to a worm with up to 4 segments | Body shape |
| **Legs** | 0–4 pairs of wiggling legs | Faster, but burns more energy |
| **Tail** | None to a long, wagging fin | Faster, but burns more energy |
| **Eyes** | Tiny to large eyes | Sees food from farther away |
| **Color** | Main color plus a second color | Drifts gradually between generations |
| **Tone** | Pale pastel to deep, rich color | Drifts gradually between generations |
| **Pattern** | Plain, stripes, spots or a saddle patch | Changes only through rare big mutations, so families keep their pattern |

Tap any creature to see its generation, age, pattern, genes and how full it is.

## Controls

| Gesture | Action |
|---------|--------|
| **Tap the water** | Drop food |
| **Tap a creature or egg** | Inspect it |
| **Swipe (any direction)** | Clean the tank |
| **Hold 1–5 s, release** | Tank status: day, population, highest generation, water quality |
| **Hold > 5 s, release** | Start a brand-new tank (**replaces your save**) |

## Saving

The tank saves itself to the ESP32's flash memory 10 seconds after you interact with it, and every 5 minutes regardless. **Unplugging pauses time.** When you plug it back in, it says "Welcome back!" and continues exactly where it left off. You lose at most a few minutes.

Everything that needs saving lives in one `World` struct, so saving is a single `putBytes` call. When the struct changes in a code update, a size and version check starts a fresh tank instead of loading data that no longer matches.

## Speeding up time

`TIME_SCALE`, at the top of the sketch, speeds up biology (hunger, aging, eggs and rotting) but not swimming:

| Value | One tank-day takes |
|-------|--------------------|
| `1` | 24 hours (real time) |
| `150` | about 10 minutes (the setting in this file) |
| `600` | about 2.5 minutes (good for testing) |

After changing it, hold for 5 seconds to start a fresh tank.

## Code map

The sketch is divided into labeled sections:

- **STAGE 1:** genes become bodies and behavior (procedural drawing using forward and sideways vectors)
- **STAGE 2:** hunger, feeding and dirty water
- **STAGE 3:** eggs, growing up, aging and mutation
- **STAGE 4:** saving and loading

The balance values (food energy, egg cost, how fast dirt builds up and others) are constants under **Tuning knobs** at the top. They were tuned by simulating several weeks of tank life at different feeding levels, which caught an early version that boomed and then collapsed even with generous feeding.
