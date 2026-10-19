# Game 1: Pocket Ecosystem

<!-- <img src="../media/ecosystem.gif" width="240"> -->

An artificial-life sandbox. Herbivores and predators live on a round 40×40 grid, compete for food, and evolve as they reproduce. Nothing is scripted: population booms, crashes and changes in the gene pool all come from a handful of simple rules.

## How the world works

- **A new world** has 40 herbivores with random genes, scattered food, and 3 randomly placed **hazard zones**. Each new world uses a fresh random seed.
- **Food regrows** on its own every tick. Hazard zones grow food twice as fast but drain the energy of anything standing in them, which makes them a risk-versus-reward choice.
- **Every organism burns energy** just by living. Faster and farther-sighted organisms burn more. This cost is what drives evolution: no single strategy wins everywhere.
- **Herbivores** move toward the nearest food they can see and run from predators, with a burst of speed while fleeing.
- **Predators** hunt herbivores. Only 35% of attacks succeed, a miss costs energy, and predators burn energy 1.6× faster than herbivores.
- **Reproduction:** when an organism's energy passes its reproduction threshold, it splits in two. The child gets half the energy and a slightly mutated copy of the parent's genes.
- **Death:** organisms that starve become food. Organisms that are eaten don't.

## Genes

| Gene | Effect |
|------|--------|
| **Speed** | Chance of moving each tick. Faster organisms find food sooner but burn more energy. |
| **Vision** | How far away food or danger can be seen (1–7 cells). Seeing farther costs energy. |
| **Reproduction threshold** | Energy needed to split. Low means many weak offspring; high means fewer, stronger ones. |
| **Caution** | Chance of refusing to step into a hazard. |

## Reading the screen

- **Herbivores** are squares whose **color shows their genes**: red = speed, green = vision, blue = caution. If the population shifts color over time, selection is at work.
- **Predators** are red circles with a white center.
- **Green cells** are food. **Dark magenta** areas are hazards, which turn **orange** when food grows in them.
- **Top:** `H` = herbivores, `P` = predators, `T` = temperature, `M` = mutation rate.
- **Bottom graph:** population over time, with herbivores in green and predators in red.

## Controls

| Gesture | Action |
|---------|--------|
| **Tap** | Drop a patch of food |
| **Hold ~1 s, release** | Release 3 predators where you touched (the ring turns orange) |
| **Hold > 2.5 s, release** | New world: new seed, new hazards, no predators (the ring turns red) |
| **Swipe up / down** | Raise or lower the temperature (0.25–3.0), which multiplies everyone's energy cost |
| **Swipe right / left** | Raise or lower the mutation rate (0–0.5) |

## Watching evolution

Open **Tools → Serial Plotter** in the Arduino IDE to graph population counts and the herbivores' average speed, vision and caution over time. The screen shows what's happening right now; the plotter shows trends over thousands of ticks.

## Things to try

- **Start a predator-prey cycle:** let herbivores grow, add predators, then don't touch anything. Watch the two lines rise and fall in turn.
- **Turn up the heat:** raise the temperature and watch whether average speed falls as moving becomes expensive.
- **Freeze evolution:** set the mutation rate to 0 and compare how the world behaves without adaptation.
- **Remove a cost:** in the code, delete the speed term from the energy cost. Without a cost, does speed climb to the maximum?

## Tuning knobs

The main balance values are constants at the top of the sketch (`FOOD_PER_TICK`, `CATCH_CHANCE`, `PRED_GAIN`, `FLEE_BOOST` and others), each with a comment. Change one at a time and watch the graph for a few minutes.

## Known limitations

- **Herbivores can fill every organism slot (300 maximum).** When that happens the herbivore line goes flat, and holding to add predators does nothing because there's no free slot. Starting a new world clears it. A planned fix caps herbivores below the maximum so there is always room for predators.
- The world doesn't save. Unplugging the board ends the experiment.
