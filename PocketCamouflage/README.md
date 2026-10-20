# Game 3: Pocket Camouflage

<!-- <img src="../media/camouflage.gif" width="240"> -->

Predators hunt by sight. Creatures whose colors blend into the background are harder to spot, so they survive and breed, and over generations the population evolves camouflage. Switch the biome and watch the population struggle, then re-adapt.

This game brings together the first two: **the predators and experimenting from Pocket Ecosystem**, and **the creatures, genes and bodies from Pocket Terrarium**. The new idea is that **appearance now affects survival**.

> **Status:** Complete. Its biomes and sight-based hunting carry forward into [Game 4: Pocket Arms Race](../PocketArmsRace/), where predators evolve too.

## How it works

- **Biomes.** The seafloor is a patchwork of three colors that depend on the biome: **Kelp Forest** (greens), **Coral Reef** (sand, coral and purple) or **Deep Water** (dark blues).
- **Food grows on its own,** so this is an experiment to watch, not a pet to care for. Creatures eat, grow, lay eggs and die in a matter of minutes, so generations go by quickly.
- **Predators hunt by sight.** A predator patrols the tank, and its chance of noticing a creature depends on how much that creature **contrasts with the patch it's on**:
  - Below a small contrast threshold, a creature is effectively invisible. Above a high one, it can't be missed. This is modeled on real vision.
  - **Movement gives creatures away:** moving creatures are 1.6× easier to spot. Well-fed creatures stay still, and hungry ones have to search for food.
  - **Bigger creatures** are easier to see.
  - **Patterns** mix in the second color and slightly break up the outline.
  - Camouflaged prey can **vanish mid-chase** when the predator loses track of them.
- **Creatures flee** when a predator chasing them comes within range of their eyes, curving away from the glass so they don't get cornered. Speed, legs and tails still matter.
- **Deaths leave a brief ring:** red means eaten, grey means old age, and brown means starved.
- **Children inherit their parent's genes with mutations.** Creatures that blend in survive longer and leave more offspring, so the population's colors drift toward the background.

## Genes

The genes are the same as in Pocket Terrarium (size, body length, legs, tail, eyes, two colors, tone and pattern), plus one new one:

| Gene | Effect |
|------|--------|
| **Light** (new) | Brightness, from dark to bright. Necessary for blending into Deep Water or pale sand. |

## Reading the screen

- **Top:** the biome name, the population, and the **average camouflage**. That last number is the one to watch.
- **The predator** is the long, dark creature. Its **eyes** show its mood: orange means patrolling, red means chasing (with a **!** when it first spots something), and dim means resting after a meal.
- **Light-green dots** are algae (food).
- **Eggs** are tinted with the parent's color, so they're camouflaged too.

## Controls

| Gesture | Action |
|---------|--------|
| **Tap the water** | Drop food |
| **Tap a creature** | Inspect it: generation, camouflage score, genes, and whether it's moving |
| **Swipe left / right** | Change the biome (the creatures stay, so can they adapt?) |
| **Swipe up / down** | Add or remove a predator (0–3) |
| **Hold 1–4 s, release** | Status: biome, population, creatures eaten, highest generation |
| **Hold > 4 s, release** | New world |

## Experiments to try

1. **Watch camouflage evolve.** Leave one predator running for 20 to 30 minutes and watch the average camouflage climb.
2. **Run a control.** Swipe down to 0 predators, start a new world, and compare. Without predators, camouflage just drifts.
3. **Change the environment.** After the population has adapted, swipe to a new biome. Camouflage crashes, then slowly recovers as the population re-adapts.
4. **Increase the pressure.** With 3 predators, selection is stronger and adaptation is faster.
5. **Inspect the survivors.** Tap creatures after a long run. The best-camouflaged lineages often aren't the ones you'd guess by eye.

Open **Tools → Serial Plotter** to graph camouflage, population and creatures eaten over time.

## Tuning knobs

The main values are at the top of the sketch. The predator settings matter most:

| Knob | What it does |
|------|--------------|
| `SEE_MIN` / `SEE_MAX` | The contrast range a predator can see. This sets how strong selection is. |
| `SPOT_RATE` | How quickly a predator notices visible creatures |
| `MOVE_VISIBILITY` | How much moving gives a creature away |
| `LOSE_RATE` | How easily camouflaged prey escape mid-chase |
| `REST_TIME` | How long a predator digests after a meal |
| `SIM_SPEED` | Set to `2` or `3` to make everything run faster |

## Design notes

These rules were tuned by simulating hour-long runs on a computer, with and without predators. The first version had a **linear** detection rule: twice the contrast meant twice the chance of being spotted. Under that rule the predator spotted creatures almost at random, and camouflage never evolved. Switching to a **threshold** rule, where low contrast is invisible and high contrast is obvious, is what made selection work. Average camouflage now rises from about 35–50% without predators to about 63–75% with one predator, and it usually recovers within about 30 minutes of a biome change. Big jumps, like bright Coral Reef to dark Deep Water, take longer.

A second bug showed up on real hardware: fleeing creatures got trapped circling the edge of the tank. Their "run from the predator" rule and their "turn back from the glass" rule fought each other, and nearly half of all kills happened at the wall, even though creatures spent only 7% of their time there. Fleeing creatures now blend in a push away from the wall, which dropped wall kills from 40 to 2 per 20 minutes. Escaping got easier, so the predator was tuned to be faster and more persistent to keep selection strong.

## Ideas not built (yet)

- A color ring around the edge of the screen showing the population's color mix
- Gradual "seasons" that slowly shift the background colors
- Predators that learn to spot whichever color is most common, giving rare colors an advantage
