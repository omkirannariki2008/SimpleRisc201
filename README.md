# RISC201 / SimpleRISC Processor Simulator & Verification Environment

A cycle-accurate 32-bit RISC processor simulator, software toolchain, and architectural evaluation suite for Computer Organization & Architecture.

---

## 📋 Table of Contents
1. [Overview & Execution Engines](#-overview--execution-engines)
2. [Package & Directory Structure](#-package--directory-structure)
3. [How to Build & Compile](#-how-to-build--compile)
4. [How to Run (Command Reference)](#-how-to-run-command-reference)
5. [Interactive CLI Debugger Guide](#-interactive-cli-debugger-guide)
6. [Active ASCII Stack Visualizer](#-active-ascii-stack-visualizer)
7. [Subsystem & Architectural Innovations](#-subsystem--architectural-innovations)
8. [Sample Assembly Test Suite](#-sample-assembly-test-suite)

---

## 🔬 Overview & Execution Engines

The simulator implements four complete execution paradigms and a complete system software suite:

1. **4-Stage Pipelined Processor** (`IF` $\rightarrow$ `ID/RR` $\rightarrow$ `EX` $\rightarrow$ `MEM/WB`):
   - Baseline pipeline with full data forwarding (EX/MEM, MEM/WB), internal register file write-before-read bypass, and load-use hazard interlocks.
2. **6-Stage Super-Pipelined Processor** (`IF1` $\rightarrow$ `IF2` $\rightarrow$ `OF` $\rightarrow$ `EX` $\rightarrow$ `MA` $\rightarrow$ `RW`):
   - Canonical decoupled pipeline separating memory access (`MA`) from register writeback (`RW`), eliminating critical path bottlenecks and enabling higher clock frequencies ($f_{\text{max}}$).
3. **45-Bit Vertical Microprogrammed Control Unit**:
   - Models a 19-register shared internal bus driven by an encoded micro-ROM with a microinstruction decoder and $\mu$PC dispatch logic.
4. **64-Bit Horizontal Microprogrammed Control Unit**:
   - An unencoded wide control store driving datapath multiplexers, tri-state drivers, and latch enables directly with 0 ns decoder delay, achieving a **1.81x instruction retirement speedup**.

---

## 📁 Package & Directory Structure

```text
SimpleRisc201/
├── include/
│   ├── alu.hpp                     # 32-bit ALU declarations & hardware arithmetic algorithms
│   ├── assembler.hpp               # Two-pass assembler & static load-use optimizer
│   ├── cpu_pipeline.hpp            # 4-stage/6-stage pipeline, RAS, BTB, L1 caches, Store Buffer
│   ├── debugger_cli.hpp            # Interactive step-by-step CLI execution debugger
│   ├── disassembler.hpp            # Binary/hex machine code disassembler
│   ├── exception_framework.hpp     # Hardware boundary safety & runtime exception traps
│   ├── horizontal_control_unit.hpp # 64-bit unencoded horizontal micro-control unit
│   ├── stack_preprocessor.hpp      # Full-descending macro preprocessor & consolidated push/pop
│   ├── stack_visualizer.hpp        # Real-time ASCII stack memory frame renderer
│   ├── types.hpp                   # ISA specification, 24 opcodes, 4 formats, flags & registers
│   └── vertical_control_unit.hpp   # 45-bit encoded vertical micro-control unit
├── src/
│   ├── alu.cpp                     # Booth, CLA with GPK, Newton-Raphson, IEEE-754 FPU, CLZ
│   ├── assembler.cpp               # Two-pass encoding & out-of-order instruction hoisting
│   ├── cpu_pipeline.cpp            # Pipelined datapath execution, latches, hazards, forwarding
│   ├── debugger_cli.cpp            # Interactive debugger commands & live inspection
│   ├── disassembler.cpp            # Disassembly formatting & instruction decoding
│   ├── exception_framework.cpp     # Memory limits, division-by-zero, unaligned access traps
│   ├── horizontal_control_unit.cpp # Direct gate-drive microcode execution & sequencer
│   ├── main.cpp                    # Integrated evaluation suite driver & comparative scoreboard
│   ├── stack_preprocessor.cpp      # Macro expansion & block frame allocation engine
│   ├── stack_visualizer.cpp        # Formatted ASCII stack memory tables
│   └── vertical_control_unit.cpp   # Vertical micro-ROM & shared-bus arbitration
├── programs/
│   ├── demo.s                      # General execution demo (arithmetic, moves, comparisons)
│   ├── test_datapath_advanced.s    # Subroutines, RAS, store-to-load forwarding, L1 cache hits
│   ├── test_exceptions.s           # Division-by-zero & stack boundary hardware traps
│   ├── test_hazards.s              # Data forwarding stress, load-use stalls, branch flushes
│   ├── test_load_use_opt.s         # Static instruction scheduling & 100% stall elimination
│   └── test_stack_macros.s         # Nested stack frames, multi-register push/pop, stack engine
├── Makefile                        # Multi-target build script
├── .gitignore                      # Git exclusion rules
├── ANALYSIS_REPORT.md              # In-depth architectural analysis & engineering report
└── README.md                       # Complete operational guide & quick reference
```

---

## 🛠️ How to Build & Compile

The project requires a standard C++14 (or newer) compiler (`g++` or `clang++`).

```bash
# Clean previous builds and compile
make clean && make
```

This compiles all components into the optimized executable `simplerisc_sim.exe`.

---

## 🚀 How to Run (Command Reference)

### 1. Run the Complete Integrated Evaluation Suite
Runs all ALU hardware tests, preprocessor tests, assembler/disassembler tests, stack visualizer, exception traps, microcode comparisons, and pipeline benchmarks:
```bash
./simplerisc_sim.exe
```

### 2. Run a Specific Assembly Program Directly
To bypass self-tests and run directly on an assembly file:
```bash
./simplerisc_sim.exe --no-tests programs/demo.s
```

### 3. Select Pipeline Execution Mode
You can select which pipeline architecture to evaluate using the `--mode` (`-m`) flag:
```bash
# Run in 4-Stage Pipeline mode
./simplerisc_sim.exe --no-tests --mode 4 programs/test_hazards.s

# Run in 6-Stage Super-Pipeline mode
./simplerisc_sim.exe --no-tests --mode 6 programs/test_hazards.s

# Run both side-by-side with an architectural comparative scoreboard
./simplerisc_sim.exe --no-tests --mode both programs/test_hazards.s
```

### 4. Command-Line Options Summary
```text
Usage: ./simplerisc_sim.exe [options] [file.s]

Options:
  --cli, -i, --debugger       Launch interactive CLI debugger
  --mode, -m <4|6|both>       Set pipeline execution mode (default: both)
  --no-tests                  Skip verification test suites and run file/CLI directly
  --help, -h                  Display help message
```

---

## 💻 Interactive CLI Debugger Guide

Launch the debugger with any assembly file:
```bash
./simplerisc_sim.exe --cli programs/test_stack_macros.s
```

### Debugger Commands Menu

| Command | Shorthand | Description |
|:---|:---:|:---|
| `step [n]` | `s [n]` | Advance simulation by $n$ clock cycles (default: 1). |
| `next` | `n` | Macro-step: run until current instruction finishes writeback. |
| `run` | `c` | Run continuously until a breakpoint, halt, or exception occurs. |
| `break <pc>` | `b <pc>` | Set breakpoint at specified address (hex e.g. `b 0x20` or decimal). |
| `del <pc>` | | Delete breakpoint at specified address. |
| `breaks` | | Display all active breakpoints. |
| `regs` | `r` | Dump all 16 registers (`r0`–`r15`), `PC`, and condition flags. |
| `pipe` | `p` | Inspect pipeline latches, active instructions, and forwarding wires. |
| `stack [d]` | `st [d]` | Render live ASCII stack memory frame up to depth $d$. |
| `mem <addr> [n]` | | Dump $n$ words of memory starting at `<addr>`. |
| `disasm [pc] [n]`| | Disassemble $n$ instructions starting at `<pc>`. |
| `mode <4\|6\|micro>`| | Switch execution mode on the fly between 4-stage, 6-stage, and microcode. |
| `reset` | | Reset registers, memory, and pipeline latches. |
| `help` | `h` | Display debugger command reference. |
| `quit` | `q` | Exit the debugger session. |

---

## 📊 Active ASCII Stack Visualizer

The simulator includes a real-time ASCII stack visualizer for full-descending memory frames:

```text
+-----------------------------------------------------------------------------+
|                 ACTIVE STACK MEMORY FRAME (FULL DESCENDING)                 |
+-----------------------------------------------------------------------------+
| SP: 0x00003fe8 | Top: 0x00003ffc | Limit: 0x00002000 | Used: 5 words (0.2%) |
+------------+------------+-------------+--------------+----------------------+
| Byte Addr  | Offset(SP) | Hex Content | Dec (Signed) | Stack Frame Tag      |
+------------+------------+-------------+--------------+----------------------+
| 0x00003fe8 | +  0B       | 0x00000000   |            0  | <== SP [TOS]         |
| 0x00003fec | +  4B       | 0x00000000   |            0  |                      |
| 0x00003ff0 | +  8B       | 0x0000001e   |           30  |                      |
| 0x00003ff4 | + 12B       | 0x00000014   |           20  |                      |
| 0x00003ff8 | + 16B       | 0x0000000a   |           10  |                      |
| 0x00003ffc | + 20B       | 0x00000000   |            0  | [Stack Origin / Base]|
+------------+------------+-------------+--------------+----------------------+
| Stack Direction: High Memory (0x3ffc) ===> Decrementing Downward (SP: 0x3fe8)    |
+-----------------------------------------------------------------------------+
```

### Column Explanations:
* **`SP`**: Current hardware stack pointer register (`r14`).
* **`Byte Addr`**: Physical 32-bit RAM byte address.
* **`Offset(SP)`**: Positive offset from current `SP` (e.g. `+8B` corresponds to `8[sp]`).
* **`Hex Content` & `Dec (Signed)`**: Value stored in memory in both hexadecimal and signed decimal.
* **`Stack Frame Tag`**: Highlights `<== SP [TOS]` (Top of Stack) and `[Stack Origin / Base]`.

---

## ⚙️ Subsystem & Architectural Innovations

### 1. 32-Bit Arithmetic Logic Unit (`alu.cpp`)
* **Hierarchical CLA Adder with GPK Logic**: Implements bit-level Generate ($G$), Propagate ($P$), and an explicit **Kill ($K$) barrier** ($K_i = \sim(A_i | B_i)$). When both bits are 0, carry-out is unconditionally clamped to 0 without waiting for carry-in. Evaluates carries in $\mathcal{O}(\log N) \approx 5$ gate delays.
* **Radix-4 Modified Booth Multiplier**: Evaluates a 3-bit sliding window $(B_{2i+1}, B_{2i}, B_{2i-1})$ with native two's complement arithmetic, cutting 32-bit multiplication iterations by 50% (from 32 to 16 iterations).
* **Newton-Raphson Integer Divider & Modulo**: Uses iterative reciprocal approximation with a minimax initial seed ($X_0$) achieving 53-bit precision in 5 double-precision iterations. Evaluates exact single-cycle modulo via $R = A - (Q \times B)$.
* **IEEE-754 Single-Precision FPU**: Floating-point multiplication (Figure 8.18) with Round-to-Nearest-Even (RNE) and Newton-Raphson division.
* **Priority-Encoder CLZ**: 5-level binary priority encoder evaluating Count Leading Zeros in $\mathcal{O}(\log_2 32) = 5$ gate delays for instant integer $\leftrightarrow$ double bit conversions (`I2D`, `D2I`).

### 2. Pipelined Datapath & Hazard Management (`cpu_pipeline.cpp`)
* **Return Address Stack (RAS)**: 16-entry circular hardware LIFO stack speculatively predicting subroutine return addresses at fetch time, reducing `ret` penalties to **0 cycles** (100% accuracy on structured code).
* **Dynamic 2-Bit Saturating Branch Predictor & 64-Entry BTB**: Transitions across 4 states, eliminating loop branch penalties after the first iteration.
* **Split Harvard L1 Caches**: Dedicated 2-way set-associative 512-byte Instruction and Data caches with LRU replacement, eliminating Von Neumann structural hazards.
* **Hardware Store Buffer & Forwarding Queue**: 4-entry associative FIFO queue bypassing store data directly to dependent loads with 0 memory cycle latency.
* **Front-End Hardware Stack Engine**: Dedicated adder in Decode resolving `SP` adjustments in 0 cycles, freeing the integer ALU from pointer bookkeeping.

### 3. Assembler Static Load-Use Optimization (`assembler.cpp`)
* Two-pass assembler includes an **out-of-order instruction scheduling pass**.
* Scans basic blocks for load-use hazards, checks dependency rules (RAW, WAR, WAW, and status flags), and hoists independent instructions into load delay slots.
* **Impact**: Completely eliminates load-use pipeline stalls (**100% reduction, +11.8% speedup** on load-heavy code) while guaranteeing identical register results.

---

## 📂 Sample Assembly Test Suite

Located in the `programs/` folder:
* **`demo.s`**: Basic arithmetic, register movement, comparison, and branching.
* **`test_hazards.s`**: Data dependency stress test with back-to-back RAW hazards, load-use stall, and branch flushes.
* **`test_load_use_opt.s`**: Benchmark demonstrating static instruction scheduling and 100% load-use stall elimination.
* **`test_datapath_advanced.s`**: Subroutine calls, RAS 0-bubble returns, store-to-load forwarding, and L1 cache hits.
* **`test_stack_macros.s`**: Nested subroutines, consolidated multi-register push/pop, and stack engine absorption.
* **`test_exceptions.s`**: Hardware exception verification testing divide-by-zero traps without simulator termination.
