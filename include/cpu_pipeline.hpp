#ifndef SIMPLERISC_CPU_PIPELINE_HPP
#define SIMPLERISC_CPU_PIPELINE_HPP

#include "types.hpp"
#include "alu.hpp"
#include "vertical_control_unit.hpp"
#include "pipeline_visualizer.hpp"
#include <string>
#include <vector>
#include <array>
#include <memory>
#include <set>
#include <sstream>
#include <iomanip>

namespace simplerisc {

enum class PipelineMode {
    FOUR_STAGE,       // 4-Stage: IF -> ID/RR -> EX -> MEM/WB
    SIX_STAGE,        // 6-Stage Super-Pipeline: IF1 -> IF2 -> OF -> EX -> MA -> RW
    MICROPROGRAMMED   // Microprogrammed Shared-Bus Model
};

// ----------------------------------------------------------------------------
// Dynamic 2-Bit Saturating Branch Predictor & BTB
// ----------------------------------------------------------------------------
enum class BranchPredictionState {
    STRONGLY_NOT_TAKEN = 0,
    WEAKLY_NOT_TAKEN   = 1,
    WEAKLY_TAKEN       = 2,
    STRONGLY_TAKEN     = 3
};

struct BTBEntry {
    addr_t target{0};
    BranchPredictionState state{BranchPredictionState::WEAKLY_NOT_TAKEN};
    bool valid{false};
};

// ----------------------------------------------------------------------------
// Return Address Stack (RAS) for Speculative Subroutine Returns
// Eliminates the 2-cycle flush penalty on 'ret' instructions
// ----------------------------------------------------------------------------
class ReturnAddressStack {
public:
    static constexpr size_t RAS_CAPACITY = 16;

    ReturnAddressStack() { reset(); }

    void reset() {
        stack_.fill(0);
        top_ = 0;
        count_ = 0;
    }

    void push(addr_t ret_addr) {
        stack_[top_] = ret_addr;
        top_ = (top_ + 1) % RAS_CAPACITY;
        if (count_ < RAS_CAPACITY) count_++;
    }

    addr_t pop() {
        if (count_ == 0) return 0;
        top_ = (top_ + RAS_CAPACITY - 1) % RAS_CAPACITY;
        count_--;
        return stack_[top_];
    }

    addr_t peek() const {
        if (count_ == 0) return 0;
        size_t idx = (top_ + RAS_CAPACITY - 1) % RAS_CAPACITY;
        return stack_[idx];
    }

    bool empty() const { return count_ == 0; }
    size_t size() const { return count_; }

private:
    std::array<addr_t, RAS_CAPACITY> stack_;
    size_t top_{0};
    size_t count_{0};
};

// ----------------------------------------------------------------------------
// Front-End Hardware Stack Engine (Intel Core / ARM Style)
// Tracks SP deltas in the Decode (ID/OF) stage, eliminating ALU contention for SP
// ----------------------------------------------------------------------------
class StackEngine {
public:
    StackEngine() { reset(); }

    void reset(word_t initial_sp = 0x00003FFC) {
        current_sp_ = initial_sp;
    }

    // Check if an instruction is an explicit Stack Pointer adjustment: sub sp, sp, imm or add sp, sp, imm
    static bool isSpAdjustment(const Instruction& inst) {
        return (inst.rd == SP && inst.rs1 == SP && inst.I &&
               (inst.opcode == OP_ADD || inst.opcode == OP_SUB));
    }

    // Calculate new SP immediately in front-end
    word_t updateInDecode(const Instruction& inst, word_t base_sp) {
        if (inst.opcode == OP_SUB) {
            current_sp_ = base_sp - inst.immx;
        } else if (inst.opcode == OP_ADD) {
            current_sp_ = base_sp + inst.immx;
        }
        return current_sp_;
    }

    word_t getSP() const { return current_sp_; }
    void setSP(word_t sp) { current_sp_ = sp; }

private:
    word_t current_sp_{0x00003FFC};
};


// ----------------------------------------------------------------------------
// L1 Cache Subsystem (Harvard Architecture: Split I-Cache & D-Cache)
// Resolves Von Neumann Structural Hazard on Memory Access
// ----------------------------------------------------------------------------
struct CacheLine {
    bool valid{false};
    bool dirty{false};
    addr_t tag{0};
    uint32_t last_used{0};
    std::array<word_t, 4> data{}; // 16-byte cache line (4 words)
};

class L1Cache {
public:
    static constexpr size_t NUM_SETS = 16;
    static constexpr size_t WAYS = 2; // 2-Way Set-Associative
    static constexpr size_t LINE_WORDS = 4; // 16 bytes per line

    L1Cache() { reset(); }

    void reset() {
        for (auto& set : sets_) {
            for (auto& way : set) {
                way = CacheLine{};
            }
        }
        cycle_counter_ = 0;
    }

    bool access(addr_t byte_addr, bool is_write, word_t write_val, const std::vector<word_t>& memory, word_t& read_val) {
        cycle_counter_++;
        addr_t block_addr = byte_addr & ~0xF; // 16-byte aligned
        size_t set_idx = (block_addr >> 4) % NUM_SETS;
        addr_t tag = block_addr >> 8;
        size_t word_offset = (byte_addr >> 2) & 0x3;

        // Check for hit
        for (size_t w = 0; w < WAYS; ++w) {
            auto& line = sets_[set_idx][w];
            if (line.valid && line.tag == tag) {
                line.last_used = cycle_counter_;
                if (is_write) {
                    line.data[word_offset] = write_val;
                    line.dirty = true;
                } else {
                    read_val = line.data[word_offset];
                }
                return true; // Cache Hit
            }
        }

        // Cache Miss: LRU Replacement
        size_t victim = 0;
        uint32_t oldest = sets_[set_idx][0].last_used;
        for (size_t w = 1; w < WAYS; ++w) {
            if (!sets_[set_idx][w].valid) {
                victim = w;
                break;
            }
            if (sets_[set_idx][w].last_used < oldest) {
                oldest = sets_[set_idx][w].last_used;
                victim = w;
            }
        }

        auto& vline = sets_[set_idx][victim];
        vline.valid = true;
        vline.dirty = false;
        vline.tag = tag;
        vline.last_used = cycle_counter_;
        for (size_t i = 0; i < LINE_WORDS; ++i) {
            addr_t mem_word_idx = (block_addr / 4) + i;
            vline.data[i] = (mem_word_idx < memory.size()) ? memory[mem_word_idx] : 0;
        }

        if (is_write) {
            vline.data[word_offset] = write_val;
            vline.dirty = true;
        } else {
            read_val = vline.data[word_offset];
        }
        return false; // Cache Miss
    }

private:
    std::array<std::array<CacheLine, WAYS>, NUM_SETS> sets_;
    uint32_t cycle_counter_{0};
};

// ----------------------------------------------------------------------------
// Hardware Store Buffer (Store-to-Load Forwarding)
// Bypasses stores directly to dependent loads without waiting for D-cache write
// ----------------------------------------------------------------------------
struct StoreBufferEntry {
    addr_t address{0};
    word_t data{0};
    bool valid{false};
};

class StoreBuffer {
public:
    static constexpr size_t BUFFER_SIZE = 4;

    StoreBuffer() { reset(); }

    void reset() {
        for (auto& entry : entries_) entry = StoreBufferEntry{};
    }

    void insert(addr_t address, word_t data) {
        for (size_t i = BUFFER_SIZE - 1; i > 0; --i) {
            entries_[i] = entries_[i - 1];
        }
        entries_[0] = StoreBufferEntry{address, data, true};
    }

    bool lookup(addr_t address, word_t& out_data) const {
        for (size_t i = 0; i < BUFFER_SIZE; ++i) {
            if (entries_[i].valid && entries_[i].address == address) {
                out_data = entries_[i].data;
                return true;
            }
        }
        return false;
    }

private:
    std::array<StoreBufferEntry, BUFFER_SIZE> entries_;
};

enum class ForwardSource {
    NONE,
    EX_MEM,
    MEM_WB
};

struct PipelineMetrics {
    uint64_t total_cycles{0};         // Total execution clock / micro-cycles
    uint64_t instructions_retired{0}; // Total instructions completed writeback
    uint64_t stall_cycles{0};         // Load-use and hazard bubble stall cycles
    uint64_t branch_flushes{0};       // Branch misprediction / taken flush events
    uint64_t forwarding_events{0};     // Operand bypass forwarding occurrences
    uint64_t load_use_hazards{0};     // Detected load-use hazard events
    uint64_t branch_predictions{0};    // Branch predictor evaluations
    uint64_t branch_prediction_hits{0}; // Correct predictions (0 penalty)
    uint64_t early_branch_resolutions{0}; // Unconditional branches resolved early in ID

    // Advanced Datapath & Cache Metrics
    uint64_t ras_predictions{0};          // Subroutine return predictions via RAS
    uint64_t ras_hits{0};                 // Correct RAS return address hits (0 bubbles)
    uint64_t store_load_forwards{0};      // Store-to-Load buffer bypass hits
    uint64_t icache_accesses{0};          // L1 I-Cache access attempts
    uint64_t icache_hits{0};              // L1 I-Cache hits
    uint64_t dcache_accesses{0};          // L1 D-Cache access attempts
    uint64_t dcache_hits{0};              // L1 D-Cache hits
    uint64_t structural_stalls_avoided{0};// Memory ports structural hazards eliminated
    uint64_t stack_engine_resolutions{0}; // Front-end Stack Engine SP adjustments resolved

    double getPredictionAccuracy() const {
        return (branch_predictions > 0) ? (static_cast<double>(branch_prediction_hits) / branch_predictions * 100.0) : 0.0;
    }
    double getRasAccuracy() const {
        return (ras_predictions > 0) ? (static_cast<double>(ras_hits) / ras_predictions * 100.0) : 0.0;
    }
    double getICacheHitRate() const {
        return (icache_accesses > 0) ? (static_cast<double>(icache_hits) / icache_accesses * 100.0) : 0.0;
    }
    double getDCacheHitRate() const {
        return (dcache_accesses > 0) ? (static_cast<double>(dcache_hits) / dcache_accesses * 100.0) : 0.0;
    }

    double getCPI() const {
        return (instructions_retired > 0) ? static_cast<double>(total_cycles) / instructions_retired : 0.0;
    }
    double getIPC() const {
        return (total_cycles > 0) ? static_cast<double>(instructions_retired) / total_cycles : 0.0;
    }

    std::string dump() const;
};

// ----------------------------------------------------------------------------
// 4-Stage Pipeline Inter-Stage Registers
// ----------------------------------------------------------------------------
struct Latch_IF_ID {
    addr_t pc{0};
    word_t raw_inst{0};
    bool predicted_taken{false};
    addr_t predicted_target{0};
    addr_t fallthrough_pc{0};
    bool ras_predicted{false};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_ID_EX {
    addr_t pc{0};
    Instruction inst;
    word_t val_rs1{0};
    word_t val_rs2{0};
    word_t val_rd{0};
    bool predicted_taken{false};
    addr_t predicted_target{0};
    addr_t fallthrough_pc{0};
    bool ras_predicted{false};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_EX_MEM {
    addr_t pc{0};
    Instruction inst;
    word_t alu_result{0};
    word_t store_data{0};
    bool branch_taken{false};
    addr_t branch_target{0};
    ForwardSource fwd_a{ForwardSource::NONE};
    ForwardSource fwd_b{ForwardSource::NONE};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_MEM_WB {
    addr_t pc{0};
    Instruction inst;
    word_t write_data{0};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

// ----------------------------------------------------------------------------
// 6-Stage Super-Pipeline Inter-Stage Registers (IF1 -> IF2 -> OF -> EX -> MA -> RW)
// ----------------------------------------------------------------------------
struct Latch_IF1_IF2 {
    addr_t pc{0};
    bool predicted_taken{false};
    addr_t predicted_target{0};
    addr_t fallthrough_pc{0};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_IF2_OF {
    addr_t pc{0};
    word_t raw_inst{0};
    bool predicted_taken{false};
    addr_t predicted_target{0};
    addr_t fallthrough_pc{0};
    bool ras_predicted{false};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_OF_EX {
    addr_t pc{0};
    Instruction inst;
    word_t val_rs1{0};
    word_t val_rs2{0};
    word_t val_rd{0};
    bool predicted_taken{false};
    addr_t predicted_target{0};
    addr_t fallthrough_pc{0};
    bool ras_predicted{false};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_EX_MA {
    addr_t pc{0};
    Instruction inst;
    word_t alu_result{0};
    word_t store_data{0};
    bool branch_taken{false};
    addr_t branch_target{0};
    ForwardSource fwd_a{ForwardSource::NONE};
    ForwardSource fwd_b{ForwardSource::NONE};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

struct Latch_MA_RW {
    addr_t pc{0};
    Instruction inst;
    word_t write_data{0};
    bool valid{false};
    bool bubble{false};
    uint64_t inst_id{0};
};

// ----------------------------------------------------------------------------
// CPUPipeline Class
// ----------------------------------------------------------------------------
class CPUPipeline {
public:
    explicit CPUPipeline(PipelineMode mode = PipelineMode::FOUR_STAGE);
    virtual ~CPUPipeline() = default;

    void reset();
    void loadProgram(const std::vector<word_t>& words, addr_t text_start = TEXT_BASE);

    // Mode management
    void setPipelineMode(PipelineMode mode);
    PipelineMode getPipelineMode() const { return mode_; }

    // Execution methods
    bool cycleStep(); // Execute 1 cycle in active mode
    bool macroStep(); // Execute until 1 instruction completes WB or halts
    void run(uint64_t max_cycles = 10000000);

    // State queries
    bool isHalted() const { return halted_; }
    void halt() { halted_ = true; }
    bool hasException() const { return has_exception_; }
    std::string getExceptionMessage() const { return exception_msg_; }

    word_t getRegister(reg_t r) const;
    void setRegister(reg_t r, word_t val);

    addr_t getPC() const { return pc_; }
    void setPC(addr_t pc) { pc_ = pc; }

    Flags getFlags() const { return flags_; }
    void setFlags(const Flags& flags) { flags_ = flags; }

    word_t readMemory(addr_t addr) const;
    void writeMemory(addr_t addr, word_t val);

    const PipelineMetrics& getMetrics() const { return metrics_; }
    void resetMetrics() { metrics_ = PipelineMetrics{}; }

    // Visualizers
    std::string dumpPipelineState() const;
    std::string dump4StageState() const;
    std::string dump6StageState() const;
    std::string dumpRegisters() const;
    std::string dumpTrace() const;
    std::string dumpSpaceTimeDiagram() const;
    std::vector<PipelineStageSnapshot> getStageSnapshots() const;

    const std::vector<CycleSnapshot>& getCycleHistory() const { return cycle_history_; }
    void clearCycleHistory() { cycle_history_.clear(); next_inst_id_ = 0; }

private:
    bool step4StageCycle();
    bool step6StageCycle();
    bool stepMicroCycle();
    void recordCycleSnapshot();

    bool is4StageEmpty() const;
    bool is6StageEmpty() const;

    PipelineMode mode_{PipelineMode::FOUR_STAGE};

    // Architectural state
    std::array<word_t, NUM_REGISTERS> regs_;
    addr_t pc_{TEXT_BASE};
    Flags flags_;
    std::vector<word_t> mem_; // Word-addressed 32-bit memory array (64KB = 16384 words)

    addr_t text_start_{TEXT_BASE};
    addr_t text_end_{TEXT_BASE};

    bool halted_{false};
    bool has_exception_{false};
    std::string exception_msg_;
    PipelineMetrics metrics_;

    // Advanced Datapath Hardware Units
    static constexpr size_t BTB_SIZE = 64;
    std::array<BTBEntry, BTB_SIZE> btb_;
    ReturnAddressStack ras_;
    StackEngine stack_engine_;
    StoreBuffer store_buffer_;
    L1Cache icache_;
    L1Cache dcache_;

    void updateBranchPredictor(addr_t pc, bool taken, addr_t target);
    std::pair<bool, addr_t> queryBranchPredictor(addr_t pc) const;

    // Baseline microprogrammed control unit for comparative benchmarks
    VerticalControlUnit cu_;

    // 4-Stage Latches
    Latch_IF_ID  if_id_;
    Latch_ID_EX  id_ex_;
    Latch_EX_MEM ex_mem_;
    Latch_MEM_WB mem_wb_;

    // 6-Stage Latches (IF1 -> IF2 -> OF -> EX -> MA -> RW)
    Latch_IF1_IF2 if1_if2_;
    Latch_IF2_OF  if2_of_;
    Latch_OF_EX   of_ex_;
    Latch_EX_MA   ex_ma_;
    Latch_MA_RW   ma_rw_;

    // Hazard & Forwarding Telemetry State
    bool last_stall_{false};
    bool last_flush_{false};
    ForwardSource last_fwd_a_{ForwardSource::NONE};
    ForwardSource last_fwd_b_{ForwardSource::NONE};

    // Cycle history for space-time trace diagrams
    std::vector<CycleSnapshot> cycle_history_;
    uint64_t next_inst_id_{0};
};

} // namespace simplerisc

#endif // SIMPLERISC_CPU_PIPELINE_HPP
