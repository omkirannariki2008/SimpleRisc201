# SimpleRISC Pipeline: Easy Guide to Findings & Optimizations

Welcome! This document explains all the discoveries, fixes, and improvements made to our processor pipeline in **plain, simple English**. 

Whether you are preparing for a viva, writing a project report, or reviewing the code, this guide breaks down complex hardware concepts into easy-to-understand real-world analogies.

---

## 📖 Quick Glossary (Read this first!)

To make everything easy to follow, here are 6 key terms explained simply:

1. **Pipeline (Assembly Line):** Instead of waiting for one instruction to completely finish before starting the next, we break execution into stages—like a car assembly line. While one car gets painted, the next is getting its engine installed.
2. **Clock Cycle:** A single tick of the processor's heartbeat. In each tick, every stage in the pipeline takes one step forward.
3. **CPI (Cycles Per Instruction):** How many clock ticks it takes on average to finish an instruction. **Lower is better!** (e.g., a CPI of 1.5 means 10 instructions take about 15 cycles).
4. **Stall (Pause / Bubble):** When an instruction must wait because its data isn't ready yet (e.g., waiting for data to load from memory). An empty slot called a "bubble" is inserted.
5. **Forwarding (Bypassing):** Passing a finished answer directly by hand from one worker to another on the assembly line, without waiting to store it on the shelf (registers) first.
6. **Flush (Squash):** When the CPU guessed wrong on a branch (`beq`/`bgt`), it cancels the wrongly fetched instructions and starts over at the correct address.

---

## 📊 Summary Table of Discoveries & Fixes

| ID | Title (What was it?) | Where in Code? | The Problem in Simple Words | How We Fixed It |
|:---:|:---|:---|:---|:---|
| **[FIND-001](#find-001-the-stack-starting-point-sp)** | **Stack Starting Point** | [src/cpu_pipeline.cpp:32](src/cpu_pipeline.cpp#L32) | Memory has 64 KB of space, but the stack was placed at 16 KB, leaving 48 KB unused. | Placed the stack at the very top of memory so deep functions won't run out of room. |
| **[FIND-002](#find-002-upgrading-to-the-canonical-6-stage-pipeline)** | **6-Stage Assembly Line** | [src/cpu_pipeline.cpp](src/cpu_pipeline.cpp) | Two big jobs (Memory Read + Register Write) were crammed into one stage, slowing the clock. | Split the work into 6 balanced stages (`IF1 -> IF2 -> OF -> EX -> MA -> RW`). Clock runs faster! |
| **[FIND-003](#find-003-passing-the-return-address-ra-by-hand)** | **Subroutine Return Address (`RA`)** | [include/types.hpp](include/types.hpp) | When calling a function, `r15` gets updated. If a return happened right away, it read the old `r15`. | Taught the pipeline to forward `r15` immediately so returns always jump to the right place. |
| **[FIND-004](#find-004-connecting-the-fast-cache-to-instruction-fetch)** | **Fast Memory (L1 Cache) Hookup** | [src/cpu_pipeline.cpp](src/cpu_pipeline.cpp) | Stage `IF2` was accidentally bypassing the fast L1 cache and reading slow memory. | Hooked `IF2` directly to the L1 cache so instructions load much faster and hit rates are tracked. |
| **[FIND-005](#find-005-easy-side-by-side-pipeline-comparison-modes)** | **Comparison Modes (`--mode`)** | [src/main.cpp](src/main.cpp) | You couldn't easily compare the 4-stage and 6-stage pipelines side by side. | Added `--mode 4`, `--mode 6`, and `--mode both` to print a clear performance scoreboard. |
| **[FIND-006](#find-006-consolidated-block-pushpop)** | **Consolidated Block Push/Pop** | [src/stack_preprocessor.cpp](src/stack_preprocessor.cpp) | Pushing 4 registers was generating 8 separate instructions with repeated pointer math. | Decrements the stack pointer once by 16 bytes and stores items using direct offsets. |
| **[FIND-007](#find-007-front-end-hardware-stack-engine)** | **Front-End Hardware Stack Engine** | [src/cpu_pipeline.cpp](src/cpu_pipeline.cpp) | Stack pointer math was clogging the main ALU in the EX stage. | Added a front-end hardware unit in Decode/OF that calculates stack shifts instantly in 0 cycles. |

---

## 🔍 Detailed Explanations

---

### FIND-001: The Stack Starting Point (`SP`)

* **File:** [`src/cpu_pipeline.cpp:32`](src/cpu_pipeline.cpp#L32)

#### 1. What was happening?
The processor has 64 Kilobytes (65,536 bytes) of memory. But the Stack Pointer (`SP` / `r14`) was initialized to address `0x00003FFC` (which is only 16 KB).

#### 2. Why was it a problem?
* Think of memory like a 4-story building. 
* The stack grows downwards (from the top floor towards the ground floor).
* Starting at 16 KB meant we were storing our stack on the **1st floor** instead of the top floor (`64 KB`).
* The upper 48 KB of memory was sitting completely empty, and deep recursion or nested functions could prematurely run out of stack space.

#### 3. How we solved it:
We pointed the stack pointer to the very top of memory (`MEMORY_SIZE - 4` = `0x0000FFFC`). Now programs have the full memory space available to grow downwards safely.

---

### FIND-002: Upgrading to the Canonical 6-Stage Pipeline

* **Files:** [`include/cpu_pipeline.hpp`](include/cpu_pipeline.hpp) & [`src/cpu_pipeline.cpp`](src/cpu_pipeline.cpp)

#### 1. What was happening?
In our earlier design, one single stage was trying to do both **Memory Access** (reading/writing RAM) AND **Register Writeback** (saving the answer). 

#### 2. Why was it a problem?
* **The Assembly Line Bottleneck:** Imagine an assembly line where 5 workers take 1 minute each, but one worker has to do two huge tasks taking 2 minutes. The whole factory must slow down to 2 minutes per tick!
* Reading RAM takes time, and writing registers takes time. Doing both in one cycle meant the processor clock had to tick slowly.

#### 3. How we solved it:
We implemented the canonical 6-stage decoupled super-pipeline:

```
[ IF1 ] ➔ [ IF2 ] ➔ [ OF ] ➔ [ EX ] ➔ [ MA ] ➔ [ RW ]
Address    Fetch     Read      ALU     Memory   Register
Gen        Inst     Regs      Math     Access   Write
```

1. **`IF1` (Instruction Fetch 1):** Calculate the address of the next instruction (PC).
2. **`IF2` (Instruction Fetch 2):** Read the 32-bit instruction word from the fast L1 Cache.
3. **`OF` (Operand Fetch):** Decode the instruction, read input registers, and check for stalls.
4. **`EX` (Execute):** Perform math in the ALU (`add`, `sub`, etc.) and test branch conditions.
5. **`MA` (Memory Access):** Read or write data in RAM (`ld`, `st`).
6. **`RW` (Register Write):** Save the final result into the destination register.

#### 4. The Benefit:
Because each stage does a smaller, balanced chunk of work, the processor can run at a **substantially higher clock speed (more MHz/GHz)**!

---

### FIND-003: Passing the Return Address (`RA`) by Hand

* **Files:** [`include/types.hpp`](include/types.hpp) & [`src/cpu_pipeline.cpp`](src/cpu_pipeline.cpp)

#### 1. What was happening?
When your program calls a function with `call my_func`, the CPU automatically saves the return location into register `r15` (`RA` = Return Address), so the function knows where to jump back when it finishes.

#### 2. Why was it a problem?
* In a pipeline, an instruction is still moving through the stages when the next instruction starts.
* The old pipeline didn't realize that `call` creates a new value in `r15`.
* If a function immediately tried to read `r15` or execute `ret` (return), it would read the **old, stale** return address instead of the new one! The CPU would jump to the wrong place or crash.

#### 3. How we solved it:
1. We taught the CPU that `ret` reads `r15` (`Instruction::readsRA()`).
2. We added a **Forwarding Wire (Bypass)**: As soon as `call` calculates the return address in the `EX` stage, it immediately hands that value directly to the next instruction in `OF` without waiting for it to be written to memory or registers.

---

### FIND-004: Connecting the Fast Cache to Instruction Fetch

* **File:** [`src/cpu_pipeline.cpp:731`](src/cpu_pipeline.cpp#L731)

#### 1. What was happening?
The processor includes an ultra-fast **L1 Instruction Cache** (like keeping books right on your desk instead of walking to the library every time you need a sentence). 
However, stage `IF2` was accidentally bypassing this cache and reading directly from main memory.

#### 2. Why was it a problem?
* The cache wasn't doing its job! Cache hit rates were not being recorded for instruction fetches in the 6-stage pipeline.
* Main memory is slow; bypassing the cache defeated the purpose of having fast hardware.

#### 3. How we solved it:
* Connected `IF2` to query `icache_.access()` first.
* Enabled **Harvard Architecture tracking**: Instructions can now be fetched from the I-Cache at the exact same cycle that data is read from the D-Cache, completely avoiding memory traffic jams (structural hazards).

---

### FIND-005: Easy Side-by-Side Pipeline Comparison Modes

* **File:** [`src/main.cpp`](src/main.cpp)

#### 1. What was happening?
The program was hardcoded to only run assembly files in the 4-stage pipeline. To test the 6-stage pipeline, you had to manually change C++ code.

#### 2. How we solved it:
We added easy command-line flags:
* `./simplerisc_sim.exe file.s --mode 4` $\rightarrow$ Runs in 4-Stage Pipeline mode.
* `./simplerisc_sim.exe file.s --mode 6` $\rightarrow$ Runs in 6-Stage Super-Pipeline mode.
* `./simplerisc_sim.exe file.s --mode both` $\rightarrow$ Runs both and prints a beautiful side-by-side scoreboard!

#### Example Scoreboard Output:
```text
+-----------------------+---------------+---------------+---------------+---------------+
| Architecture Model    | Clock Cycles  | Retired Inst  | CPI           | Speedup vs CU |
+-----------------------+---------------+---------------+---------------+---------------+
| Microprogrammed (CU)  |           144 |            13 |         11.08 |         1.00x |
| 4-Stage Pipeline      |            19 |            12 |          1.58 |         7.00x |
| 6-Stage Super-Pipeline|            21 |            12 |          1.75 |         6.33x |
+-----------------------+---------------+---------------+---------------+---------------+
```

---

### FIND-006: Consolidated Block Push/Pop

* **File:** [`src/stack_preprocessor.cpp`](src/stack_preprocessor.cpp)

#### 1. What was happening?
Pushing 4 registers with `push {r1, r2, r3, r4}` was generating 8 individual instructions: alternating `sub sp, sp, 4` followed by `st r, 0[sp]`. 

#### 2. Why was it a problem?
Constantly modifying `SP` back and forth created repeated data dependency stalls on the stack pointer register.

#### 3. How we solved it:
We consolidated the stack adjustment into a single block allocation:
* Decrements `SP` once: `sub sp, sp, 16`
* Stores the 4 registers directly using fixed offsets: `st r1, 12[sp]`, `st r2, 8[sp]`, etc.
* Achieves a **37.5% reduction in instruction count** on 4-register pushes.

---

### FIND-007: Front-End Hardware Stack Engine

* **File:** [`src/cpu_pipeline.cpp`](src/cpu_pipeline.cpp)

#### 1. What was happening?
Every time code modified the stack pointer (`sub sp, sp, 16`), it occupied the main ALU in the `EX` stage, making other math instructions wait.

#### 2. How we solved it:
Added a dedicated **Stack Engine** directly inside the Decode/Operand Fetch stage:
* Detects stack adjustments and calculates the new `SP` immediately in the front-end (0 cycles).
* Converts the original instruction into a `nop` for the ALU, freeing the ALU to focus on calculations.
* Tracked live in execution metrics as `Stack Engine Absorbed`.

---

### FIND-008: Load-Use Delay Optimization (Static Out-of-Order Instruction Scheduling)

* **Files:** [`src/assembler.cpp`](src/assembler.cpp), [`include/assembler.hpp`](include/assembler.hpp)

#### 1. What was happening? (The Problem)
When the CPU reads data from memory with `ld r1, 0[r2]`, the value is not ready until the Data Memory (`MA`) stage. If the very next instruction immediately needs that data (`add r3, r1, r4`), forwarding cannot deliver it in time. The pipeline must freeze (stall for 1 cycle), wasting clock cycles.

#### 2. How we solved it:
We added a static lookahead instruction scheduler directly into the assembler:
* Looks ahead inside the same basic block to find an independent instruction sitting lower down (e.g., `mov r5, 10`).
* Rigorously checks all data dependencies (RAW, WAR, WAW, and status flags) to ensure program math is unaffected.
* Moves (hoists) the independent instruction into the 1-cycle delay gap between the `ld` and the consumer.
* The independent instruction executes for free while memory is reading, completely eliminating the 1-cycle stall!

#### 3. Benchmark Comparison (`programs/test_load_use_opt.s`):

| Performance Metric | Unoptimized (Base) | Optimized (Scheduled) | Architectural Improvement |
|:---|:---:|:---:|:---|
| **Load-Use Hazards Trapped** | 2 | **0** | **-100.0% (Zero Hazards)** |
| **Pipeline Stall Cycles** | 2 | **0** | **-100.0% (Zero Stalls)** |
| **Total Execution Cycles** | 19 | **17** | **2 Clock Cycles Saved** |
| **Instructions Retired** | 12 | 12 | Exact Instruction Parity |
| **Cycles Per Instruction (CPI)** | 1.583 | **1.417** | **Lower / More Efficient CPI** |
| **Relative Speedup** | 1.000x | **1.118x** | **+11.8% Faster Execution** |
| **Register Value Verification** | `r1=25, r3=50, r5=100...` | `r1=25, r3=50, r5=100...` | **100% Identical Final State** |

---

## 💡 Summary: Why does the 6-Stage Pipeline Win?

You might notice that for a short program, the 6-stage pipeline takes 21 clock cycles while the 4-stage takes 19 clock cycles. 

**Does that mean 6-stage is slower? No! Here is why:**

$$\text{Total Time} = \frac{\text{Instructions} \times \text{CPI}}{\text{Clock Frequency (Speed)}}$$

1. **Cycle count is slightly higher** because it takes 5 cycles to fill up a 6-stage pipeline at the very start (like filling a longer water pipe before water comes out the other end).
2. **BUT each stage does half the work**, meaning the clock can tick **nearly twice as fast** (e.g., 2.0 GHz instead of 1.0 GHz).
3. Therefore, even with a tiny increase in cycle count, the **real-world execution time is significantly faster**!
