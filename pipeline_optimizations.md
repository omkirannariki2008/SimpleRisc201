# RISC201 Pipeline & Datapath Architectural Innovations

**Course:** Computer Organization & Architecture (CSC-201)  
**Architecture Reference:** 32-Bit SimpleRISC Microarchitecture  
**Authors:** SimpleRisc201 Architecture Team  

This document provides a technical breakdown of the **Pipeline and Datapath Bottleneck Optimizations** implemented in the SimpleRISC simulator. These enhancements address hardware hazards, eliminate pipeline bubbles, resolve structural bus conflicts, and improve execution throughput.

---

## 1. Executive Summary & Overview of Optimizations

The baseline pipelined datapath suffered from several classic microarchitectural bottlenecks common to textbook designs:

| Bottleneck Identified | Root Cause in Baseline Design | Advanced Solution Implemented | Primary Benefit |
|---|---|---|---|
| **1. Return Penalty on `ret`** | Function returns (`ret`) waited until the late `EX` stage to read `r15` (`RA`), forcing a **2-cycle pipeline flush** on every subroutine return. | **Return Address Stack (RAS)**: Circular 16-entry hardware LIFO stack with speculative return target prediction in `IF`. | **0-cycle return penalty** (100% accuracy on non-corrupted call stacks); eliminates pipeline bubbles on returns. |
| **2. Von Neumann Structural Hazard** | Single flat memory array (`mem_`) was accessed concurrently by `IF` (instruction fetch) and `MEM` (data load/store) in the same clock cycle. | **Split Harvard L1 Caches**: Dedicated 2-Way Set-Associative L1 I-Cache and L1 D-Cache with LRU replacement. | Eliminates single-port memory contention; provides realistic cache hit/miss latency modeling. |
| **3. Store-to-Load Latency** | A `ld` immediately following a `st` to the same memory address had to wait for the memory write to commit before reading back the value. | **Hardware Store Buffer (Store-to-Load Forwarding)**: 4-entry FIFO store queue with associative address matching. | Bypasses stored data directly to dependent loads within the pipeline without memory stalls. |
| **4. Late Branch Resolution & Loop Flushes** | Conditional and unconditional branches evaluated in `EX`, causing a 2-cycle bubble on every taken branch. | **Dynamic 2-Bit Saturating Branch Predictor & 64-Entry BTB** with early unconditional resolution in `ID`. | Loops predict "Taken" with **0 penalty cycles**; unconditional jumps resolve with only 1 cycle penalty. |
| **5. Canonical 6-Stage Pipeline (`IF1->IF2->OF->EX->MA->RW`)** | Merged `MEM/RW` stage in earlier models forced data memory access and register writeback into a single cycle, creating a severe clock frequency critical path. | **Canonical Super-Pipeline Structure** strictly separating Data Memory Access (`MA`) and Register Writeback (`RW`), with a dedicated `OF` stage. | Eliminates timing critical path, enables higher clock frequencies, and maintains canonical 1-cycle load-use hazard handling. |
| **6. Subroutine Link Register Corruption** | `Instruction::writesRd()` erroneously treated `OP_CALL` as writing a GPR rather than the link register `r15` (`RA`). | **Corrected `writesRd()` and link register update** in `types.hpp` and pipeline writeback latches. | Enables nested subroutine calls and correct return address tracking across function frames. |

---

## 2. Deep-Dive: Optimization Details & Hardware Implementations

### A. Return Address Stack (RAS) for Speculative Returns

```
                   +--------------------------------------------+
  call target ===> | Push (PC + 4) onto Circular LIFO Stack     |
                   +--------------------------------------------+
                                         |
                                         v
                               [RAS: 16-Entry Array]
                                         |
                   +--------------------------------------------+
  ret fetched ===> | Pop Top Address & Speculatively Redirect PC | ===> 0 Penalty Bubbles!
                   +--------------------------------------------+
```

#### The Problem
In standard RISC pipelines, unconditional jumps (`b`) and calls (`call`) have static branch targets encoded directly in the instruction word (immediate offset or label). However, subroutine returns (`ret`) jump to the dynamic address stored in register `r15` (`RA`). Because register reading occurs in `ID/RR` and branch redirect occurs in `EX`, every `ret` instruction flushed the preceding stages, wasting 2 clock cycles.

#### Hardware Implementation (`include/cpu_pipeline.hpp`, `src/cpu_pipeline.cpp`)
1. **Hardware Stack Structure:**
   - 16-entry circular buffer managed by a stack pointer (`top_`) and occupancy counter (`count_`).
2. **Push on `call` (Decode Stage):**
   - When `OP_CALL` is decoded in `ID`, the return address (`if_id_.pc + 4`) is pushed onto the RAS.
3. **Speculative Fetch on `ret` (Fetch Stage):**
   - The fetch stage pre-decodes the opcode bits (`raw >> 27`). If the instruction is `OP_RET` and the RAS is non-empty, `ras_.pop()` immediately supplies the return target, redirecting `pc_` in the very next cycle.
4. **Execution Stage Verification (`EX`):**
   - When `ret` reaches `EX`, the actual return register `regs_[RA]` is compared with the predicted target.
   - **On Match (RAS Hit):** `branch_taken` is cleared, and `last_flush_` is set to `false`. **Zero penalty bubbles are inserted.**
   - **On Mismatch:** If the stack was corrupted or underflowed, the pipeline falls back to flushing and redirecting to `regs_[RA]`.

---

### B. Split Harvard L1 Caches (I-Cache & D-Cache)

```mermaid
flowchart TD
    subgraph "Processor Core"
        IF_STAGE["Fetch Stage (IF)"]
        MEM_STAGE["Memory Stage (MEM)"]
    end

    subgraph "L1 Harvard Cache Hierarchy"
        ICACHE["L1 Instruction Cache<br/>(2-Way Set-Associative, 16 Sets, LRU)"]
        DCACHE["L1 Data Cache<br/>(2-Way Set-Associative, 16 Sets, LRU)"]
    end

    MAIN_MEM[("64 KB Physical Main Memory")]

    IF_STAGE -->|"Parallel Fetch Port"| ICACHE
    MEM_STAGE -->|"Parallel Load/Store Port"| DCACHE
    ICACHE -.->|"Line Fill on Miss"| MAIN_MEM
    DCACHE -.->|"Write-Through / Line Fill"| MAIN_MEM
```

#### The Problem
In simple simulators, physical memory is modeled as a flat array. In real hardware, accessing the same single-port memory simultaneously from both the Instruction Fetch (`IF`) and Data Memory Access (`MEM`) stages produces a **structural hazard**. The pipeline must stall one of the two accesses unless split caches or dual-ported SRAMs are provided.

#### Hardware Implementation
1. **Cache Organization:**
   - **Sets:** 16 sets per cache.
   - **Associativity:** 2-way set-associative.
   - **Line Size:** 16 bytes (4 32-bit words per line).
   - **Replacement Policy:** True Least-Recently-Used (LRU) tracking via hardware timestamp counters.
2. **Concurrent Dual-Port Operation:**
   - The Instruction Cache services `IF` while the Data Cache concurrently services `MEM`.
   - Structural hazard avoidance is measured in real-time via `structural_stalls_avoided` telemetry.

---

### C. Hardware Store Buffer & Store-to-Load Forwarding

```
  Stage EX/MEM: [Store: Addr 200, Data 100] ---> Insert into Store Buffer (Entry 0)
                                                       |
                                                       | (Immediate Bypass)
                                                       v
  Stage EX/MEM: [Load: Addr 200]             <--- Match! Forward 100 directly!
                                                  (No D-Cache/Memory read needed)
```

#### The Problem
When a program spills a register to the stack (`st r1, 0[sp]`) and immediately reloads it into another register (`ld r2, 0[sp]`), the store has not yet completed its physical memory write cycle when the load executes. Without forwarding, the load must stall until the write commits.

#### Hardware Implementation
1. **Store Buffer Queue:** A 4-entry FIFO buffer holding `{address, data, valid}` tuples.
2. **Associative Address Lookup:**
   - When a load enters the memory stage, its target address is queried against the active Store Buffer entries from newest to oldest.
3. **Bypass:** If an address match occurs, the load takes the buffered data immediately with zero memory cycle delay, incrementing `metrics_.store_load_forwards`.

---

### D. Dynamic 2-Bit Saturating Branch Predictor & Branch Target Buffer (BTB)

```mermaid
stateDiagram-v2
    [*] --> WEAKLY_NOT_TAKEN
    STRONGLY_NOT_TAKEN --> WEAKLY_NOT_TAKEN : Taken
    WEAKLY_NOT_TAKEN --> STRONGLY_NOT_TAKEN : Not Taken
    WEAKLY_NOT_TAKEN --> WEAKLY_TAKEN : Taken
    WEAKLY_TAKEN --> STRONGLY_TAKEN : Taken
    WEAKLY_TAKEN --> WEAKLY_NOT_TAKEN : Not Taken
    STRONGLY_TAKEN --> WEAKLY_TAKEN : Not Taken
```

- **BTB Size:** 64 direct-mapped entries indexed by `(PC >> 2) % 64`.
- **Early Unconditional Branch Resolution:** Unconditional branches (`b`, `call`) are resolved early in the `ID` stage, reducing mispredicted redirect penalty from 2 cycles to 1 cycle.
- **Loop Acceleration:** On backward loop branches, the predictor quickly locks into `STRONGLY_TAKEN`, eliminating pipeline flushes on all interior loop iterations.

---

## 3. Comparative Performance & Hardware Metrics

### Benchmark: Subroutine Call / Return with Store-to-Load Hazard
Tested on `programs/test_datapath_advanced.s`:

```
+-----------------------------------------------------------------------------+
|                     HARDWARE EXECUTION METRICS                              |
+-----------------------------------------------------------------------------+
| Total Clock Cycles:               15                                        |
| Instructions Retired:              9                                        |
| Pipeline Stall Cycles:             0                                        |
| Branch Flush Penalties:            2                                        |
| Forwarding (Bypass) Hits:          1                                        |
| Load-Use Hazards Trapped:          0                                        |
| Branch Predictor Hits:             1                                        |
| RAS Return Hits:                   1                                        |
| RAS Accuracy:                 100.0%                                        |
| Early Branch (ID) Hits:            2                                        |
| Store-Load Forwards:               1                                        |
| L1 I-Cache Hit Rate:           66.7% (6/9)                                  |
| Structural Haz Avoided:            1                                        |
| CPI (Cycles / Inst):           1.667                                        |
| IPC (Throughput):              0.600                                        |
+-----------------------------------------------------------------------------+
```

### Benchmark: 5-Iteration Loop Accumulator
Tested on loop code with backward branch repetition:
- **Baseline (No Predictor):** Flushes on every iteration ($\text{CPI} \approx 1.88$).
- **Optimized (BTB + RAS):** 9 branch hits, 0 return penalties, $\text{CPI} = 1.212$ (**+35.5% throughput improvement**).

---

## 4. Summary of Code Changes

| File | Changes Made |
|---|---|
| [`include/cpu_pipeline.hpp`](include/cpu_pipeline.hpp) | Added `ReturnAddressStack`, `L1Cache`, `StoreBuffer`, `BTBEntry`, `BranchPredictionState`, and extended `PipelineMetrics`. |
| [`src/cpu_pipeline.cpp`](src/cpu_pipeline.cpp) | Integrated RAS push/pop in `ID`/`IF`, store-to-load forwarding in `MEM`/`WB`, L1 cache tracking in `IF`/`MEM`, and early resolution in `ID`. |
| [`include/types.hpp`](include/types.hpp) | Fixed `writesRd()` to exclude `OP_CALL` so link register `r15` is updated properly. |
| [`src/main.cpp`](src/main.cpp) | Added automated test suite `testDatapathOptimizations()` verifying RAS, Store-Forwarding, and L1 caches. |
| [`programs/test_datapath_advanced.s`](programs/test_datapath_advanced.s) | Added assembly test program for advanced datapath verification. |
| [`src/disassembler.cpp`](src/disassembler.cpp), [`src/stack_visualizer.cpp`](src/stack_visualizer.cpp) | Fixed `std::left` stream manipulator leak causing hex value formatting errors. |
