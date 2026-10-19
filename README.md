# ESP32-Darwinism
A series of interactive organism based sketches built for an ESP32 S3 round touch display.
These games are iterative, with each new game building on the foundations of the previous one.

# Game 1: Pocket Ecosystem
The game is built on a random seed. It generates a random number if herbivores(prey).
Food auto replenishes, hazards are also randomly generated when a new world seed is created.

User Inputs: Tapping once adds food for the herbivores. This helps to increase their spawn rate.
             Holding your finger on the screen for about 1 second creates predators in the environment. 
                 The predator limit is capped to keep them from quickly overtaking the entire environment
             Holding for 2.5 seconds creates a new world with new hazards and
                 and resets the predator count to 0
             Swiping Uo or Down changes the temperature, affecting metabolism cost
             Swiping Left or Right changes the mutation rate

Screenshots: 
