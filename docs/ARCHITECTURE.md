# Wedding Cake Hash Store Specification

## Architecture Overview

The **Wedding Cake Store** is an append-only, tiered forward and reverse hash lookup database. It handles
chronologically supplied hashes indexed by monotonically increasing **Global Presentation Indices** (`GlobalPi`),
which may contain gaps.

````
       ┌─────────────────────────────────────────┐
       │               HashStore                 │
       │   (Handles duplicates & GlobalPi gaps)  │
       └────┬──────────────────────────────┬─────┘
            │                              │
┌───────────▼───────────┐      ┌───────────▼───────────┐
│       Main Cake       │      │  Known-Duplicates Cake│
│   (Unique hashes)     │      │   (First-seen Pi's)   │
└───────────┬───────────┘      └───────────┬───────────┘
            │                              │
            └──────────────┬───────────────┘
                           │
                  ┌────────▼────────┐
                  │    DupStore     │
                  │ (Twin-sister Pi)│
                  └─────────────────┘
````
---
## 1. Core Interfaces & Concepts

### `HashStore` Concept
The public-facing container that manages duplicate detection and delegates unique entries down to underlying `Cake`
instances.

* **`InsertHashDupDetect(hash, globalPi)`**:
    1. Performs a `ReverseLookup` on the **Known-Duplicates Cake**. If found, records `(firstGlobalPi, globalPi)`
       into `DupStore` and returns `firstGlobalPi`.
    2. If not found, performs a `ReverseLookup` on the **Main Cake**. If found, inserts `firstGlobalPi` into
       **Known-Duplicates Cake**, appends `(firstGlobalPi, globalPi)` into `DupStore`, and returns `firstGlobalPi`.
    3. If missing from both: Inserts `(hash, globalPi)` into the **Main Cake**.
* **`ForwardLookup(globalPi)`**: Resolves `globalPi` to `(hash, bool)`. Handles twin-sister lookup via `DupStore`
                                 mapping if necessary.
* **`ReverseLookup(hash)`**: Returns the earliest `globalPi` associated with the given hash, or `NO_MATCH`.

---

### `SubStore` Concept
A read-only or composite interface providing lookup over a specific range of global presentation indices.
SubStores do not support direct insertion.

* **`ForwardLookup(globalPi)`**: Returns `(hash, bool)` indicating presence and the associated hash.
* **`Offset()`**: Returns the starting `GlobalPi` (`PiOffset`) for this sub-store.
* **`Count()`**: Returns the total number of presentation index slots covered.
* **`ReverseLookupCandidate(hash)`**: Fast path lookup. Examines partial hashes/fingerprints and returns a
                                      `LocalPi` candidate, or `NO_MATCH`.

---

### `DupStore` Concept
Stores relationships between primary `GlobalPi` entries and duplicate ("twin sister") `GlobalPi` instances
which were presented with the same hash.

* **`Append(firstGlobalPi, newDuplicateGlobalPi)`**: Maps a primary index to a secondary duplicate index.
* **`PrimaryFor(duplicateGlobalPi)`**: Resolves a duplicate index back to its primary index.
* **`Others(firstGlobalPi)`**: Returns an ordered list of all duplicate `GlobalPi`s associated with the primary index.

---

## 2. Dynamic Storage Hierarchy
````
Cake (Active Tier N)  ──[Seals when full]──► Sealed Tier N ──► Baker Bakes ──► Donut in Tier N+1
│                                                                                  │
├── Input Tier (In-Memory Fast Lookup + Disk)                                      └── Icer adds Trie Index folder
└── Tiers (TierN_xxx) ──► Donuts ──► Icing Sidecars
````
---

### `Input Tier`
* An in-memory, fast-lookup tier backed simultaneously to disk.
* Contains raw hashes without nested donuts.
* Serves as the active landing zone for new insertions into a `Cake`.
* When filled, it is sealed and handed over to the Baker to be compiled into a Donut in Tier 1.

---

### `Cake` Concept
The main storage coordinator. Manages active writes, tier sealing, and background baking requests.

* **`InsertHashNoDup(hash, globalPi)`**: Appends a non-duplicate hash to the active `Input Tier`.
* **Tier Management & Sealing**:
    * Tiers are stored on disk in folders named `TierN_xxx` where `xxx` is the starting `PiOffset`.
    * When a tier fills up, the Cake **seals** it (marks it read-only) and opens a new active
      tier `TierN_yyy` (where `yyy > xxx`). No folder renames are required.
* **Hot-Swapping**:
    * The Baker bakes a sealed tier into a single compiled Donut in Tier N+1.
    * Once the Baker notifies the Cake that baking and icing are complete, the Cake deletes the sealed
      Tier N folder from disk and registers the new Donut into Tier N+1.

---

### `Donut` & `Icing` Concepts

* **`Donut`**: An immutable, write-once storage block associated with a folder on disk.
    Contains a "lookup by presentation index" file, containing LSB "tail bits" and an index
    into a "Prefixes" file associated with those tail bits. Each entry in the Prefixes file
    contains the other bytes of the hash, and the presentation index.
* **`Icing`**: A trie-index sidecar stored in a separate folder alongside its corresponding Donut.
* **Verification Loop**:
    1. `Icing` executes a fast candidate match via `ReverseLookupCandidate(hash)`.
    2. The candidate `LocalPi` is converted to `GlobalPi`.
    3. The `Donut` verifies the candidate by calling `ForwardLookup(GlobalPi)`. If the full hash matches, the search
       completes; if false, searching continues down remaining Donuts/Tiers.

---

## 3. Worker & Process Roles

### `Baker` Process
An asynchronous background task commanded by the `Cake`.

1. Reads a sealed `TierN_xxx` folder from disk.
2. Compiles the tier's contents into a new single `Donut` assigned to Tier N+1.
3. Triggers the **Icer** to generate the fast-lookup trie index folder for the new Donut.
4. Notifies the `Cake` when both the Donut and Icing are fully flushed and ready on disk.
