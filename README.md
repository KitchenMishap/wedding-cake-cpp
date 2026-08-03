# wedding-cake-cpp

> A lightweight, cache-friendly C++20 compact trie and hash indexing engine.

`wedding-cake-cpp` is a native, hardware-adaptive indexing engine designed for 160-bit, 256-bit, and 512-bit
cryptographic hashes. It is the C++ implementation of the architecture proven in
[`wedding-cake`](https://github.com/KitchenMishap/wedding-cake).

### Core Philosophy

* **Hardware-Adaptive:** Thread-lean and zero-copy via `mmap`. Scales dynamically from single-board
computers (Raspberry Pi) to high-memory server environments.
* **Low Disk Churn & Low Write Amplification:** Employs write-once, immutable file layouts that minimize disk wear
and flash memory degradation.
* **Efficient Use of Disk Space:** Typically uses only 50% overhead over the raw hashes themselves
* **Deterministic & Checksummable:** Storage representations are strictly reproducible and self-verifying 
at every level.
* **Compact Variable-Density Trie:** Uses hybrid node formats (`Tiny`, `Medium`, `Full`, and `Leaf`) to
achieve maximum packing efficiency and single-cycle register bit-slicing.
