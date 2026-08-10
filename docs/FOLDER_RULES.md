# Wedding cake folder rules

## UNSEALED and SEALED
* Various objects (folders on disk) are either UNSEALED or SEALED
* UNSEALED folders here are indicated with "missing corners"
* SEALED folders are immutable, read only, and readable by multiple actors (threads)
* UNSEALED folders can contain SEALED folders, but not vice vera
* There are no occasions where an UNSEALED folder is deleted
* SEALED folders never become UNSEALED, they can only be deleted
````
                                      Add Contents
                                          │
                                         \│/
                                          V
              ───────────────       ───────────────      ┌─────────────┐
CREATION --> │UNSEALED object│ --> │UNSEALED object│ --> │SEALED object│  --> DELETED
             │EMPTY          │     │NON-EMPTY      │     │             │
              ───────────────       ───────────────      └─────────────┘
````
## Cake composed of Levels, Tiers, Donuts
* There is a hierarchy of Levels, composed of Tiers, composed of Donuts
* There is always a Level 0, and Levels are never destroyed
* Older hashes flow down through the Levels
* A Level always has one UNSEALED Input Tier, and sometimes one SEALED Output tier (and no other Tiers)
* There are up to N Donuts in a Tier (typically N=4)
* Each donut in Level L+1 is N (4) times as big as a Donut in level L
````
                     New
                    Hashes
                      │
             ─────────┼─────
            │        \│/    │
            │         V     │
            │┌─┐ ┌─┐  ─     │
            ││D│ │D│ │I│  - │           <-- Level 0 UNSEALED Input Tier (two SEALED Donuts, one UNSEALED Input Donut)
            │└─┘ └─┘  ─     │
            ┌───────────────┐
            │┌─┐ ┌─┐ ┌─┐ ┌─┐│
            ││D│ │D│ │D│ │D││           <-- Level 0 SEALED Output Tier
            │└─┘ └─┘ └─┘ └─┘│               
            └──────────┬────┘
                       │                <-- The whole Output Tier is being baked to become Target Donut T  
         ──────────────┼────── 
        │             \│/     │
        │              V      │
        │┌───┐ ┌───┐  ───     │
        ││ D │ │ D │ │ T │  - │       <-- Level 1 UNSEALED Input Tier
        │└───┘ └───┘  ───     │
         ───────────────────── 
 ───────────────────────────────── 
│┌───────┐ ┌───────┐ ┌───────┐    │
││   D   │ │   D   │ │   D   │  - │       <-- Level 2 Input Tier
│└───────┘ └───────┘ └───────┘    │
 ───────────────────────────────── 
````
* If a Level has an Output Tier, it is currently being baked into a bigger target donut in the next Level
* An Output Tier is SEALED (read only) and immutable
* The Output Tier is deleted when baking completes
* When an Input Tier fills:
  1) It WAITS for any pre-existing Output Tier to finish baking (it will disappear then)
  2) A new empty Input Tier is created
  3) The filled Input Tier BECOMES the Output Tier (no copy required)
  4) The (now Output Tier) is SEALED and made read only

## Level 0 Input Tier is special
* The Level 0 input tier has a system-unique Donut called the Input Donut
* Incoming hashes are inserted into the Input Donut
* When the Input Donut fills:
  1) A new empty Input Donut is created
  2) The filled Donut is no longer the Input Donut
  3) The filled Donut is SEALED (made read only)
````
           New
          Hashes              <-- New hashes go into the UNSEALED Input Donut in Level 0
            │
   ─────────┼─────
  │        \│/    │
  │         V     │
  │┌─┐ ┌─┐  ─     │
  ││D│ │D│ │I│  - │           <-- Level 0 UNSEALED Input Tier
  │└─┘ └─┘  ─     │
   ───────────────

 ````
## Object folder lifetimes
### Level 0 Donut
````
 ───────────   When Full   ┌────────────┐
│Input Donut│     -->      │SEALED Donut│  -->  DELETED as part of the parent
│in Level 0 │              │in Level 0  │       Output Tier in Level 0, once tier
 ───────────               └────────────┘       is baked into a Level 1 Donut
````
### Tier in Level L
````
                         Target Donut is being Baked into
                                         │       
 ────────────────          ──────────────┼────── 
│Empty Input Tier|        │             \│/     │
│                │        │              V      │  When ┌───────────────────────┐
│ -   -   -   -  │        │┌───┐ ┌───┐  ───     │  Full │┌───┐ ┌───┐ ┌───┐ ┌───┐│
│                │  -->   ││ D │ │ D │ │ T │  - │  -->  ││ D │ │ D │ │ D │ │ D ││  --> DELETED once baked into
│                │        │└───┘ └───┘  ───     │       │└───┘ └───┘ └───┘ └───┘│      bigger Donut in Level L+1
│                │        │ Non-empty Input Tier│       │ SEALED Output Tier    │
 ────────────────          ─────────────────────        └───────────────────────┘
````
### Donut in Level L
````
 ──────────────   When Baked   ┌────────────┐
│Target Donut  │     -->       │SEALED Donut│  -->  DELETED as part of a parent Level L
│being baked   │               │in Level L  │       Output Tier, once tier baking completes
│from Level L-1│               │            │       to a Target Donut in Level L+1
|Output Tier   │               │            │
 ──────────────                └────────────┘
````
## The Beautiful Orchestration - There's always plenty of time for a big bake!
* Baking of tiers into a new Donut in the biggest Level L is time consuming
* When we start baking, we must have a newly SEALED Output Tier in Level L-1
* That can only have happened if a bake completed from Level L-2 to Level L-1... 
* This reasoning applies all the way up to Level 0!
* WE INFER THAT A BAKE IN THE BIGGEST LEVEL ONLY STARTS WHEN ALL INPUT TIERS ABOVE ARE EMPTY
* Because baking operates from a SEALED Output Tier, it can be in a separate process while hashes continue to
  be inserted into tiers above
* There can be concurrent bakes, one per level, at any time
* The next bake into Level L will not be required until ALL Tiers above have filled (and they were empty when
  we started baking into Level L)
* BEAUTIFUL!

## Atomic UNSEALED -> SEALED transition for folders
* Folder names include an offset number: The presentation index of the first contained hash.
* A folder is deemed UNSEALED if it has the highest offset (most recent), else it is SEALED
* Sealing a donut is the atomic act of creating a new empty folder with a higher offset
* Initially the new folder is in an INITIALIZING state, as it may need files initializing to an "Empty Operational"
  configuration
* After these files are flushed, an empty INITIALIZED flag file is placed
* If on boot am apparently UNSEALED folder has no INITIALIZED file, recovery is simply a case of writing
  the "Empty Operational" files
* SEALED folders have an empty SEALED flag file placed in them, but only as a human aid
* This applies to Tier folders within a Level, and to Donut folders within the Level 0 Input Tier
* Donut folders in Level 1+, and Icing folders, take a long time to bake, so they are UNSEALED until a BAKED flag
  file is present