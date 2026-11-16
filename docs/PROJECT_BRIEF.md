# PROJECT: CUDA Craft — Complete Minecraft Java Server Engine Rewrite

You are developing an entirely new, standalone Minecraft Java Edition server implementation.

Project name: CUDA Craft. Target version: Minecraft Java Edition 1.21.11 (protocol 774), as selected by the project owner. This brief describes the long-term objective; see ROADMAP.md and VALIDATION.md for implemented scope.

**THIS IS NOT A FABRIC MOD, NOT A PLUGIN, NOT A MIXIN-BASED OPTIMIZATION, AND NOT A WRAPPER AROUND THE VANILLA SERVER.**

The goal is to completely reimplement the Minecraft Java server engine using C++20 and NVIDIA CUDA.

The finished software must operate as an independent Minecraft server executable.

## Core Requirements

1. Reimplement the complete server engine from scratch.
2. Preserve Minecraft Java Edition gameplay and protocol compatibility.
3. Use C++20 for the native server architecture.
4. Use NVIDIA CUDA for massively parallel simulation workloads.
5. Implement a deterministic, dependency-aware GPU tick scheduler.
6. Implement speculative future-tick execution.
7. Keep frequently accessed world-state data resident in GPU memory.
8. Preserve exact vanilla Redstone behavior, including zero-tick mechanics, quasi-connectivity, and update ordering.
9. Reimplement block entities, inventories, entities, AI, physics, fluids, lighting, and world generation.
10. Support existing Minecraft Java clients without client modifications.
11. Support vanilla-compatible Anvil world files and NBT data.
12. Implement Minecraft's networking, authentication, encryption, compression, and packet handling.
13. Implement chunk loading, unloading, saving, and generation.
14. Preserve vanilla chunk ticking and simulation-distance semantics.
15. Implement deterministic conflict resolution for parallel world updates.

## Speculative GPU Execution

The engine must identify predictable simulation regions and calculate future states before their authoritative ticks occur.

For example, when the authoritative server is at tick 1000, independent GPU workloads may calculate valid future states for ticks 1001–1100.

Predicted results must remain speculative until their corresponding authoritative ticks.

When an external event invalidates a prediction, affected results must be discarded or recalculated.

The system must support adaptive prediction horizons, dependency graphs, versioned snapshots, and deterministic commits.

Predicted calculations may execute concurrently only when their dependencies permit it.

## CUDA-First Architecture

Design the server around GPU execution from the beginning.

Do not simply port the original Java server architecture to C++.

Develop GPU-friendly data structures, memory layouts, work queues, and scheduling algorithms.

Use:

- CUDA kernels
- CUDA streams
- CUDA Graphs where beneficial
- GPU-resident world-state buffers
- Structure-of-arrays layouts
- Dependency-aware event scheduling
- Deterministic state transitions
- Incremental CPU/GPU synchronization
- Asynchronous I/O
- Batched network-state extraction

The CPU remains responsible for tasks better suited to it, including network I/O, filesystem access, orchestration, and workloads that cannot efficiently execute on the GPU.

## Vanilla Compatibility

The engine must reproduce observable vanilla behavior.

This includes intra-tick block updates, not merely final block states.

Zero-tick Redstone mechanisms must remain functional.

Do not simplify Redstone into conventional electrical circuit logic.

Preserve the event-driven semantics of Minecraft.

Build automated differential tests comparing the new server against a legally obtained reference Minecraft server.

Do not copy proprietary Minecraft source code into this repository.

Use clean-room implementation practices and appropriately licensed references.

## Implementation Strategy

Do not attempt to implement every Minecraft feature simultaneously.

Build a real standalone executable from the beginning.

Development order:

1. CMake/C++20/CUDA project foundation.
2. Server executable and basic network listener.
3. Minecraft protocol handshake and status responses.
4. Login, authentication, encryption, and compression.
5. Basic player connection and world synchronization.
6. World-state storage and chunk management.
7. Deterministic tick scheduler.
8. GPU block-state processing.
9. Redstone event engine.
10. GPU dependency scheduler.
11. Speculative future-tick execution.
12. Complete Redstone and piston mechanics.
13. Block entities and inventories.
14. Entity physics and collision.
15. Mob AI and pathfinding.
16. Fluids and lighting.
17. GPU world generation.
18. Full gameplay compatibility and optimization.

Each milestone must produce working code and automated tests.

Do not substitute a Fabric mod or Java server wrapper for the standalone implementation.

## Immediate Instructions

Inspect the repository and available development tools.

Create the complete project architecture.

Initialize the C++20 and CUDA build system.

Implement a standalone server executable.

Implement the Minecraft Java server status protocol as the first functional milestone.

The server should eventually appear in the Minecraft multiplayer server list and accept connections from unmodified Minecraft Java clients.

Document every implemented and missing protocol feature.

Build and test the project.

Do not claim the server is playable until login, world synchronization, and the necessary gameplay systems are implemented.

Continue implementation incrementally, prioritizing a functional standalone server over placeholder code.

**Final objective: A complete, independent, vanilla-compatible Minecraft Java server engine written in C++20 and CUDA, featuring deterministic parallel simulation and speculative future-tick execution.**
