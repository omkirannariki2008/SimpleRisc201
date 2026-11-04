#include "debugger_cli.hpp"
#include "pipeline_visualizer.hpp"
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace simplerisc {

DebuggerCLI::DebuggerCLI(CPUPipeline& pipeline)
    : pipeline_(pipeline), exceptions_(StackBounds{}) {}

void DebuggerCLI::printBanner(std::ostream& out) {
    out << "\n+=============================================================================+\n";
    out << "|         RISC201 INTERACTIVE CPU SIMULATOR & HARDWARE DEBUGGER CLI           |\n";
    out << "|                                                                              |\n";
    out << "+=============================================================================+\n";
    out << "Type 'help' or 'h' for a list of available debugger commands.\n";
}

void DebuggerCLI::printHelp(std::ostream& out) {
    out << "\n+-----------------------------------------------------------------------------+\n";
    out << "|                      RISC201 DEBUGGER COMMAND MENU                          |\n";
    out << "+-----------------------------------------------------------------------------+\n";
    out << "| -- Simulation Execution --------------------------------------------------- |\n";
    out << "| step [n] / s [n]    : Advance simulation by n clock / micro cycles (def: 1) |\n";
    out << "| next / n            : Macro-step (run until current instruction writeback)  |\n";
    out << "| run / c / continue  : Run continuously until breakpoint, halt, or exception |\n";
    out << "| reset               : Reset CPU registers, pipeline latches, and memory     |\n";
    out << "| -- Pipeline & Architectural Visualization ---------------------------------- |\n";
    out << "| pipe / p            : Display active pipeline stage latches, stalls & fwd   |\n";
    out << "| trace               : Render complete cycle stage table & space-time trace  |\n";
    out << "| diagram             : Render pipeline space-time timing diagram             |\n";
    out << "| stats               : Display hardware telemetry & execution metrics        |\n";
    out << "| hazards             : Display hazard detection & interlock telemetry        |\n";
    out << "| fwd                 : Display data forwarding (bypass) telemetry            |\n";
    out << "| cache               : Display L1 cache & memory subsystem telemetry         |\n";
    out << "| compare             : Compare 4-Stage vs 6-Stage performance on program     |\n";
    out << "| -- State & Subsystem Inspection -------------------------------------------- |\n";
    out << "| regs / r            : Dump complete register file (r0-r15) in compact grid  |\n";
    out << "| stack [d] / st [d]  : Render ASCII active stack memory frame (depth d words)|\n";
    out << "| micro / u [v|h]     : Inspect Microprogrammed Control Unit (uPC & ROM)      |\n";
    out << "| mem <addr> [cnt]    : Dump memory contents with hex, dec & disassembly      |\n";
    out << "| disasm [pc] [cnt]   : Disassemble machine instructions from memory          |\n";
    out << "| -- Breakpoints & Configuration --------------------------------------------- |\n";
    out << "| break <pc> / b <pc> : Set a breakpoint at PC address (hex or decimal)       |\n";
    out << "| del <pc>            : Delete breakpoint at specified address                |\n";
    out << "| breaks              : Display list of all active breakpoints                |\n";
    out << "| mode <4|6|micro|hz> : Switch pipeline mode (4stage, 6stage, vertical, horiz)|\n";
    out << "| trace [on|off]      : Toggle cycle-by-cycle telemetry printouts during run  |\n";
    out << "| help / h            : Show this help menu                                   |\n";
    out << "| quit / q            : Exit the interactive debugger session                 |\n";
    out << "+-----------------------------------------------------------------------------+\n";
}

void DebuggerCLI::printCurrentState(std::ostream& out) {
    word_t w = pipeline_.readMemory(pipeline_.getPC());
    DisassembledInstruction d = Disassembler::disassemble(w, pipeline_.getPC());

    std::string mode_str = (pipeline_.getPipelineMode() == PipelineMode::SIX_STAGE ? "6-Stage" :
                           (pipeline_.getPipelineMode() == PipelineMode::MICROPROGRAMMED ? "Micro" : "4-Stage"));

    out << "\n+-----------------------------------------------------------------------------+\n";
    std::ostringstream top;
    top << "PC: " << toHex8(pipeline_.getPC())
        << " | SP: " << toHex8(pipeline_.getRegister(SP))
        << " | Flags: [E=" << pipeline_.getFlags().E << " GT=" << pipeline_.getFlags().GT << "]"
        << " | Mode: " << std::setw(7) << std::left << mode_str
        << " | Cyc: " << std::dec << pipeline_.getMetrics().total_cycles;
    out << "|  " << std::left << std::setw(73) << top.str() << " |\n";

    std::ostringstream bot;
    bot << "Next Inst: " << toHex8(w) << "  ==>  " << d.full_assembly;
    out << "|  " << std::left << std::setw(73) << bot.str() << " |\n";
    out << "+-----------------------------------------------------------------------------+\n";
}

bool DebuggerCLI::executeCommand(const std::string& cmd_line, std::ostream& out) {
    std::stringstream ss(cmd_line);
    std::string cmd;
    ss >> cmd;
    if (cmd.empty()) return true;

    std::transform(cmd.begin(), cmd.end(), cmd.begin(), [](unsigned char c) { return std::tolower(c); });

    if (cmd == "q" || cmd == "quit" || cmd == "exit") {
        quit_ = true;
        out << "Exiting RISC201 Debugger. Goodbye!\n";
        return false;
    }

    if (cmd == "h" || cmd == "help") {
        printHelp(out);
        return true;
    }

    if (cmd == "s" || cmd == "step") {
        uint32_t count = 1;
        ss >> count;
        for (uint32_t i = 0; i < count; ++i) {
            if (pipeline_.isHalted()) {
                out << "CPU is halted.\n";
                break;
            }
            pipeline_.cycleStep();
            if (pipeline_.hasException()) {
                out << "[TRAP] Hardware Exception: " << pipeline_.getExceptionMessage() << "\n";
                break;
            }
            // Verify stack boundary safety on every cycle
            addr_t sp = pipeline_.getRegister(SP);
            addr_t pc = pipeline_.getPC();
            if (!exceptions_.checkStackPointer(sp, pc)) {
                out << exceptions_.dumpExceptionReport();
                break;
            }
            if (trace_mode_) {
                out << pipeline_.dumpPipelineState();
            }
            if (breakpoints_.count(pipeline_.getPC())) {
                out << "Breakpoint hit at PC 0x" << std::hex << pipeline_.getPC() << "\n";
                break;
            }
        }
        printCurrentState(out);
        return true;
    }

    if (cmd == "n" || cmd == "next") {
        if (pipeline_.isHalted()) {
            out << "CPU is halted.\n";
        } else {
            pipeline_.macroStep();
            if (pipeline_.hasException()) {
                out << "[TRAP] Hardware Exception: " << pipeline_.getExceptionMessage() << "\n";
            }
            printCurrentState(out);
        }
        return true;
    }

    if (cmd == "c" || cmd == "run" || cmd == "continue") {
        out << "Running simulation...\n";
        uint64_t cycles = 0;
        while (!pipeline_.isHalted() && cycles < 1000000) {
            pipeline_.cycleStep();
            cycles++;
            if (pipeline_.hasException()) {
                out << "[TRAP] Hardware Exception after " << std::dec << cycles << " cycles: " << pipeline_.getExceptionMessage() << "\n";
                break;
            }
            addr_t sp = pipeline_.getRegister(SP);
            addr_t pc = pipeline_.getPC();
            if (!exceptions_.checkStackPointer(sp, pc, cycles)) {
                out << exceptions_.dumpExceptionReport();
                break;
            }
            if (breakpoints_.count(pipeline_.getPC())) {
                out << "Breakpoint hit at PC 0x" << std::hex << pipeline_.getPC() << " after " << std::dec << cycles << " cycles.\n";
                break;
            }
        }
        if (pipeline_.isHalted()) {
            out << "Program finished / Halted after " << std::dec << cycles << " cycles.\n";
        }
        printCurrentState(out);
        return true;
    }

    if (cmd == "b" || cmd == "break") {
        std::string addr_token;
        if (ss >> addr_token) {
            addr_t addr = 0;
            if (addr_token.size() > 2 && (addr_token[0] == '0' && (addr_token[1] == 'x' || addr_token[1] == 'X'))) {
                addr = static_cast<addr_t>(std::stoul(addr_token.substr(2), nullptr, 16));
            } else {
                addr = static_cast<addr_t>(std::stoul(addr_token));
            }
            breakpoints_.insert(addr);
            out << "Breakpoint " << breakpoints_.size() << " set at 0x" << std::hex << addr << "\n";
        } else {
            out << "Usage: break <pc_address>\n";
        }
        return true;
    }

    if (cmd == "del") {
        std::string addr_token;
        if (ss >> addr_token) {
            addr_t addr = 0;
            if (addr_token.size() > 2 && (addr_token[0] == '0' && (addr_token[1] == 'x' || addr_token[1] == 'X'))) {
                addr = static_cast<addr_t>(std::stoul(addr_token.substr(2), nullptr, 16));
            } else {
                addr = static_cast<addr_t>(std::stoul(addr_token));
            }
            if (breakpoints_.erase(addr)) {
                out << "Breakpoint removed from 0x" << std::hex << addr << "\n";
            } else {
                out << "No breakpoint found at 0x" << std::hex << addr << "\n";
            }
        }
        return true;
    }

    if (cmd == "breaks") {
        out << "\n+-----------------------------------------------------------------------------+\n";
        out << "|                             ACTIVE BREAKPOINTS                              |\n";
        out << "+-----------------------------------------------------------------------------+\n";
        if (breakpoints_.empty()) {
            out << "| No active breakpoints configured. Use 'break <addr>' to set one.            |\n";
        } else {
            int idx = 1;
            for (addr_t bp : breakpoints_) {
                word_t w = pipeline_.readMemory(bp);
                DisassembledInstruction d = Disassembler::disassemble(w, bp);
                std::ostringstream row;
                row << "Breakpoint " << idx++ << " : " << toHex8(bp) << " (" << d.full_assembly << ")";
                out << "| " << std::left << std::setw(75) << std::setfill(' ') << PipelineVisualizer::truncateStr(row.str(), 75) << " |\n";
            }
        }
        out << "+-----------------------------------------------------------------------------+\n";
        return true;
    }

    if (cmd == "r" || cmd == "regs") {
        out << pipeline_.dumpRegisters();
        return true;
    }

    if (cmd == "st" || cmd == "stack") {
        size_t depth = 10;
        ss >> depth;
        std::vector<word_t> mem_snapshot;
        for (addr_t a = 0; a < 65536; a += 4) {
            mem_snapshot.push_back(pipeline_.readMemory(a));
        }
        out << StackVisualizer::renderWithContext(
            mem_snapshot,
            pipeline_.getRegister(SP),
            pipeline_.getPC(),
            pipeline_.getRegister(RA),
            0x00003FFC,
            0x00002000,
            depth
        );
        return true;
    }

    if (cmd == "p" || cmd == "pipe") {
        out << pipeline_.dumpPipelineState();
        return true;
    }

    if (cmd == "trace") {
        std::string opt;
        if (ss >> opt) {
            trace_mode_ = (opt == "on" || opt == "1" || opt == "true");
            out << "Cycle tracing is now " << (trace_mode_ ? "ENABLED" : "DISABLED") << "\n";
        } else {
            out << pipeline_.dumpTrace();
        }
        return true;
    }

    if (cmd == "diagram") {
        out << pipeline_.dumpSpaceTimeDiagram();
        return true;
    }

    if (cmd == "stats") {
        out << pipeline_.getMetrics().dump();
        return true;
    }

    if (cmd == "hazards") {
        out << PipelineVisualizer::renderHazards(
            pipeline_.getMetrics().stall_cycles,
            pipeline_.getMetrics().branch_flushes,
            pipeline_.getMetrics().forwarding_events,
            pipeline_.getMetrics().load_use_hazards,
            pipeline_.getMetrics().structural_stalls_avoided
        );
        return true;
    }

    if (cmd == "fwd") {
        out << PipelineVisualizer::renderForwarding(
            pipeline_.getMetrics().forwarding_events,
            pipeline_.getMetrics().store_load_forwards
        );
        return true;
    }

    if (cmd == "cache") {
        out << "\n+-----------------------------------------------------------------------------+\n";
        out << "|                   L1 CACHE & MEMORY HIERARCHY STATUS                        |\n";
        out << "+-----------------------------------------------------------------------------+\n";
        auto row = [](const std::string& lbl, const std::string& val) {
            std::ostringstream r;
            r << "| " << std::left << std::setw(38) << lbl << ": "
              << std::right << std::setw(35) << val << " |\n";
            return r.str();
        };
        const auto& m = pipeline_.getMetrics();
        std::ostringstream si, sd;
        si << std::fixed << std::setprecision(1) << m.getICacheHitRate() << "% (" << m.icache_hits << "/" << m.icache_accesses << ")";
        sd << std::fixed << std::setprecision(1) << m.getDCacheHitRate() << "% (" << m.dcache_hits << "/" << m.dcache_accesses << ")";
        out << row("L1 Instruction Cache Hit Rate", si.str());
        out << row("L1 Data Cache Hit Rate", sd.str());
        out << row("Store-to-Load Buffer Forwards", std::to_string(m.store_load_forwards));
        out << row("Structural Memory Hazards Avoided", std::to_string(m.structural_stalls_avoided));
        out << "+-----------------------------------------------------------------------------+\n";
        return true;
    }

    if (cmd == "compare") {
        if (program_words_.empty()) {
            out << "No program loaded to compare. Run the simulator with a program file first (e.g. ./simplerisc_sim.exe --cli programs/demo.s).\n";
            return true;
        }

        // Run fresh 4-stage pipeline
        CPUPipeline testP4(PipelineMode::FOUR_STAGE);
        testP4.loadProgram(program_words_, 0);
        testP4.run(500000);

        // Run fresh 6-stage pipeline
        CPUPipeline testP6(PipelineMode::SIX_STAGE);
        testP6.loadProgram(program_words_, 0);
        testP6.run(500000);

        out << PipelineVisualizer::renderComparison(
            testP4.getMetrics().total_cycles,
            testP4.getMetrics().instructions_retired,
            testP4.getMetrics().stall_cycles,
            testP4.getMetrics().branch_flushes,
            testP4.getMetrics().getCPI(),
            testP6.getMetrics().total_cycles,
            testP6.getMetrics().instructions_retired,
            testP6.getMetrics().stall_cycles,
            testP6.getMetrics().branch_flushes,
            testP6.getMetrics().getCPI(),
            0.5, 0.8
        );
        return true;
    }

    if (cmd == "u" || cmd == "micro") {
        std::string which;
        if (ss >> which && (which == "v" || which == "vert" || which == "vertical")) {
            out << vert_cu_.dumpControlState();
        } else if (which == "h" || which == "horiz" || which == "horizontal") {
            out << horiz_cu_.dumpControlState();
        } else {
            out << vert_cu_.dumpControlState() << "\n";
            out << horiz_cu_.dumpControlState();
        }
        return true;
    }

    if (cmd == "m" || cmd == "mem") {
        std::string addr_str;
        uint32_t count = 8;
        addr_t start = pipeline_.getPC();
        if (ss >> addr_str) {
            if (addr_str.size() > 2 && (addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X'))) {
                start = static_cast<addr_t>(std::stoul(addr_str.substr(2), nullptr, 16));
            } else {
                start = static_cast<addr_t>(std::stoul(addr_str));
            }
            ss >> count;
        }
        start = start & ~0x3;
        out << "\n+-----------------------------------------------------------------------------+\n";
        out << "|                           MEMORY HEX & WORD DUMP                            |\n";
        out << "+--------------+--------------+------------------+----------------------------+\n";
        out << "| Byte Address |  Value (Hex) |  Value (Signed)  | Disassembly / Text Preview |\n";
        out << "+--------------+--------------+------------------+----------------------------+\n";
        for (uint32_t i = 0; i < count; ++i) {
            addr_t cur = start + i * 4;
            word_t val = pipeline_.readMemory(cur);
            sword_t sval = static_cast<sword_t>(val);
            DisassembledInstruction dis = Disassembler::disassemble(val, cur);
            std::string dis_str = dis.full_assembly;
            if (cur == pipeline_.getPC()) dis_str += " <-- [PC]";

            out << "|  " << toHex8(cur) << "  "
                << "|  " << toHex8(val) << "  "
                << "| " << std::right << std::dec << std::setw(16) << std::setfill(' ') << sval << " "
                << "| " << std::left << std::setw(26) << std::setfill(' ') << PipelineVisualizer::truncateStr(dis_str, 26) << " |\n";
        }
        out << "+--------------+--------------+------------------+----------------------------+\n";
        return true;
    }

    if (cmd == "d" || cmd == "disasm") {
        addr_t start = pipeline_.getPC();
        uint32_t count = 8;
        std::string addr_str;
        if (ss >> addr_str) {
            if (addr_str.size() > 2 && (addr_str[0] == '0' && (addr_str[1] == 'x' || addr_str[1] == 'X'))) {
                start = static_cast<addr_t>(std::stoul(addr_str.substr(2), nullptr, 16));
            } else {
                start = static_cast<addr_t>(std::stoul(addr_str));
            }
            ss >> count;
        }
        start = start & ~0x3;
        out << "\n+-----------------------------------------------------------------------------+\n";
        out << "|                      DISASSEMBLED INSTRUCTION LISTING                       |\n";
        out << "+--------------+--------------+-----------------------------------------------+\n";
        out << "| Address      | Machine Hex  | Disassembled Instruction                      |\n";
        out << "+--------------+--------------+-----------------------------------------------+\n";
        for (uint32_t i = 0; i < count; ++i) {
            addr_t a = start + i * 4;
            word_t w = pipeline_.readMemory(a);
            DisassembledInstruction d = Disassembler::disassemble(w, a);
            std::string text = d.full_assembly;
            if (a == pipeline_.getPC()) {
                text += "   <-- [PC]";
            }
            out << "|  " << toHex8(a) << "  "
                << "|  " << toHex8(w) << "  "
                << "| " << std::left << std::setw(45) << std::setfill(' ') << PipelineVisualizer::truncateStr(text, 45) << " |\n";
        }
        out << "+--------------+--------------+-----------------------------------------------+\n";
        return true;
    }

    if (cmd == "mode") {
        std::string m;
        if (ss >> m) {
            if (m == "4" || m == "4stage") {
                pipeline_.setPipelineMode(PipelineMode::FOUR_STAGE);
                out << "Switched to 4-Stage Pipeline Mode.\n";
            } else if (m == "6" || m == "6stage") {
                pipeline_.setPipelineMode(PipelineMode::SIX_STAGE);
                out << "Switched to 6-Stage Super-Pipeline Mode.\n";
            } else if (m == "micro" || m == "vertical") {
                pipeline_.setPipelineMode(PipelineMode::MICROPROGRAMMED);
                out << "Switched to Vertical Microprogrammed Model.\n";
            } else if (m == "hz" || m == "horiz" || m == "horizontal") {
                out << "Horizontal Microprogrammed Model active.\n";
            } else {
                out << "Unknown mode: " << m << ". Valid: 4stage, 6stage, micro\n";
            }
        }
        return true;
    }

    if (cmd == "reset") {
        pipeline_.reset();
        exceptions_.clear();
        out << "CPU Pipeline, Registers, and Memory reset to initial power-on state.\n";
        return true;
    }

    out << "Unknown command: '" << cmd << "'. Type 'help' for command list.\n";
    return true;
}

void DebuggerCLI::runInteractive(std::istream& in, std::ostream& out) {
    printBanner(out);
    printCurrentState(out);

    std::string line;
    while (!quit_) {
        std::string mode_tag = "4-Stage";
        if (pipeline_.getPipelineMode() == PipelineMode::SIX_STAGE) mode_tag = "6-Stage";
        else if (pipeline_.getPipelineMode() == PipelineMode::MICROPROGRAMMED) mode_tag = "Micro";

        out << "RISC201[" << mode_tag << "]> ";
        if (!std::getline(in, line)) break;
        executeCommand(line, out);
    }
}

} // namespace simplerisc
