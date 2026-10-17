# Tiers, Evolvers, and OutputBuffers
* There is a sequence of numbered Tier's represented by folders on disk Tier0/, Tier1/...
* An empty store starts with just a Tier0/ folder; others are created as required; they are never deleted.
* At boot, the tier folders on disk are examined to populate the Tier objects in memory.
* Tier0 is responsible for accepting new Hash/PresentationIndex pairs.
* Each Tier holds zero or more Evolver objects, which each represent the hashes for a contiguous range of
    PresentationIndex's.
* Each Evolver object is represented by a subfolder TierN/Offset_<nnn>/, where nnn is the PresentationIndex of
    the first hash the Evolver is responsible for.
* An Evolver evolves through a fixed sequence of states and each state is represented by a separate type
    ("Type States")
* Each Evolver in a Tier is classified as either Evolving, or Sealed.
* There is a maximum of one Evolving Evolver in a Tier at a time, and it evolves through several states towards
    a state classified as Sealed.
* The set of Sealed Evolvers in a tier is known as the tier's OutputBuffer.
* A tier's OutputBuffer has a fixed capacity (typically 4).
* Evolvers do not move between tiers. Instead, when an OutputBuffer reaches capacity, an "ingest" instruction is
    issued to the next tier.
* An "ingest" instruction (call) to the next tier will block until that next tier's OutputBuffer is not full.
* Once that blocking completes, "ingest" in the next tier will create a new Evolver (in that next tier) that
    will have access to the previous tier's entire OutputBuffer until the Evolver has evolved to a Sealed state.
* When that Evolver is eventually Sealed, it sends a "retire" instruction to the original tier.
* In the original tier, the "retire" instruction obtains an exclusive lock on the Sealed 
    Evolvers (i.e. waits for any shared lock reads to complete) and destroys them
    (the hashes data they held is now available in the next tier).
* At any time, concurrent reads of the store can proceed by gaining shared locks to the Sealed Evolvers in each tier.
