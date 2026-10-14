Message/instructions to AI agents I am using.

I intend to use two agent subscriptions for different tasks.
If you think you are the wrong agent for the task, please remind me.
* My JetBrains subscription (included but with limited credits) is for
"using the IDE/compiler" rather than for coding or project ideas.
* My Gemini subscription (free tier under Google Workspace under an
API key) is for coding help and project ideas.

Please avoid "burning credits and ripping through my code" (I had
a bad experience with JetBrains AI in the past). I'd like to
understand this code and retain a sense of ownership.

* A THIRD agent I may refer to in chats is "GeminiWeb" which I chat
to via the Chrome. GeminiWeb has been helping me on the wedding-cake-cpp
project for some months.

Here is a description of the project from that chat in GeminiWeb:

## Subsystem Context: Evolving Disk State Framework

### 1. Overview & Purpose
The `evolving_disk_state` subsystem is a domain-agnostic, crash-proof, multi-threaded C++20 pipeline designed to process large, evolving datasets on disk with **strict bit-for-bit settled determinism** across runs. It provides durable transaction logging, background thread quiescence, and crash recovery.

### 2. Core Architectural Components
- **`ExternalFeeder`**: Batching engine that ingests raw inputs and converts them into structured work items to minimize lock contention.
- **`OutputBuffer`**: Thread-safe concurrency bridge implementing a Reader/Baker queue model. Integrates with `PipelineSettler` to track global work-in-flight counts.
- **`SliceContext`**: Oversees individual microstep executions within a work slice. Tracks open output file streams, ensures explicit flushing, and coordinates commit offsets using RAII.
- **`ManifestEngine`**: The durability engine. Maintains `manifest.tx` (a binary transaction log). On recovery (`recover_state_directory`), it inspects committed offsets and truncates incomplete or uncommitted appends back to the last clean slice boundary.
- **`TierOrchestrator`**: Manages pipeline tier execution and controls quiescence polling (`wait_for_settled()`) to ensure all background work flushes completely before tier transitions.

### 3. Critical Invariants & Constraints
1. **RAII File Lifecycle & Flushing (Windows File Locking)**:
   `SliceContext` destructors MUST explicitly flush (`stream.flush()`) user data to OS buffers *before* writing commit offsets to `ManifestEngine`, and must explicitly close streams (`stream.close()`) to avoid file handle locking on Windows.
2. **Microstep Offset Timing**:
   File stream positions (`stream.tellp()`) must be evaluated *after* worker actions write data to ensure committed byte lengths accurately reflect written content in `manifest.tx`.
3. **Quiescence Model**:
   Settled status relies on atomic work-in-flight tracking via `PipelineSettler` and polling loops (`wait_for`) to prevent pipeline bubbles from causing premature termination or race conditions.
4. **Cross-Platform Profiling Flags**:
   The `CMakeLists.txt` build configuration enforces frame pointer preservation (`-fno-omit-frame-pointer`, `-mno-omit-leaf-frame-pointer`) for profiling/flame graphs. Linker options like `-rdynamic` must strictly remain guarded (`if(UNIX AND NOT APPLE)`) as MinGW GCC on Windows will reject them.

### 4. Build & Test Commands
- **Target Executable**: `unit_tests` (GoogleTest target running all 26+ pipeline tests)
- **Profiling Executable**: `profile_shallow_tree` (Standalone profiling target built with `-O2 -g3` and disabled inlining)
- **CMake Build Command**: `cmake -B cmake-build-debug -DCMAKE_BUILD_TYPE=Debug`

I will also mention that the overall project aim is to build
a hash indexing system suitable for bitcoin and other domains.
We give it a sequence of Sha256 (or other) hashes, and on later
presentation of a hash, it returns the presentation index (sequence
number) that that hash (if any) was inserted.