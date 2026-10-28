#include "cpu_pipeline.hpp"
#include "disassembler.hpp"
#include <sstream>
#include <iomanip>
#include <iostream>

namespace simplerisc {

std::string PipelineMetrics::dump() const {
    std::stringstream ss;
    ss << "+-----------------------------------------------------------------------------+\n";
    ss << "|                     HARDWARE EXECUTION TELEMETRY                            |\n";
    ss << "+-----------------------------------------------------------------------------+\n";

    auto row = [](const std::string& lbl, const std::string& val) {
        std::ostringstream r;
        r << "| " << std::left << std::setw(38) << lbl << ": "
          << std::right << std::setw(35) << val << " |\n";
        return r.str();
    };

    auto section = [](const std::string& title) {
        std::ostringstream s;
        std::string line = "-- " + title + " ";
        if (line.size() < 75) line.append(75 - line.size(), '-');
        s << "| " << std::left << std::setw(75) << line << " |\n";
        return s.str();
    };

    ss << section("Performance Summary");
    ss << row("Total Clock Cycles", std::to_string(total_cycles));
    ss << row("Instructions Retired", std::to_string(instructions_retired));

    std::ostringstream scpi, sipc;
    scpi << std::fixed << std::setprecision(3) << getCPI();
    sipc << std::fixed << std::setprecision(3) << getIPC();
    ss << row("Cycles Per Instruction (CPI)", scpi.str());
    ss << row("Instructions Per Cycle (IPC)", sipc.str());

    ss << section("Pipeline Hazards & Bypasses");
    ss << row("Pipeline Stall Cycles (Interlocks)", std::to_string(stall_cycles));
    ss << row("Branch Flush Penalties", std::to_string(branch_flushes));
    ss << row("Forwarding (Bypass) Hits", std::to_string(forwarding_events));
    ss << row("Load-Use Hazards Trapped", std::to_string(load_use_hazards));

    if (branch_predictions > 0 || ras_predictions > 0 || early_branch_resolutions > 0) {
        ss << section("Branch & Return Prediction");
        if (branch_predictions > 0) {
            std::ostringstream sb;
            sb << branch_prediction_hits << " / " << branch_predictions << " ("
               << std::fixed << std::setprecision(1) << getPredictionAccuracy() << "%)";
            ss << row("2-Bit Saturating Branch Accuracy", sb.str());
        }
        if (ras_predictions > 0) {
            std::ostringstream sr;
            sr << ras_hits << " / " << ras_predictions << " ("
               << std::fixed << std::setprecision(1) << getRasAccuracy() << "%)";
            ss << row("Return Address Stack (RAS) Hits", sr.str());
        }
        if (early_branch_resolutions > 0) {
            ss << row("Early Branch (ID) Resolutions", std::to_string(early_branch_resolutions));
        }
    }

    if (icache_accesses > 0 || dcache_accesses > 0 || store_load_forwards > 0 || structural_stalls_avoided > 0 || stack_engine_resolutions > 0) {
        ss << section("Memory & Front-End Accelerators");
        if (store_load_forwards > 0) {
            ss << row("Store-Load Buffer Forwards", std::to_string(store_load_forwards));
        }
        if (icache_accesses > 0) {
            std::ostringstream si;
            si << std::fixed << std::setprecision(1) << getICacheHitRate() << "% (" << icache_hits << "/" << icache_accesses << ")";
            ss << row("L1 I-Cache Hit Rate", si.str());
        }
        if (dcache_accesses > 0) {
            std::ostringstream sd;
            sd << std::fixed << std::setprecision(1) << getDCacheHitRate() << "% (" << dcache_hits << "/" << dcache_accesses << ")";
            ss << row("L1 D-Cache Hit Rate", sd.str());
        }
        if (structural_stalls_avoided > 0) {
            ss << row("Structural Hazards Avoided", std::to_string(structural_stalls_avoided));
        }
        if (stack_engine_resolutions > 0) {
            ss << row("Stack Engine Absorptions", std::to_string(stack_engine_resolutions));
        }
    }

    ss << "+-----------------------------------------------------------------------------+\n";
    return ss.str();
}

CPUPipeline::CPUPipeline(PipelineMode mode)
    : mode_(mode), mem_(MEMORY_SIZE / 4, 0) {
    reset();
}

void CPUPipeline::reset() {
    regs_.fill(0);
    regs_[SP] = 0x00003FFC; // Default SP top of 16KB
    pc_ = TEXT_BASE;
    flags_ = Flags{};
    halted_ = false;
    has_exception_ = false;
    exception_msg_ = "";
    metrics_ = PipelineMetrics{};
    cycle_history_.clear();
    next_inst_id_ = 0;

    cu_.reset();

    for (auto& entry : btb_) {
        entry = BTBEntry{};
    }
    ras_.reset();
    stack_engine_.reset(0x00003FFC);
    store_buffer_.reset();
    icache_.reset();
    dcache_.reset();

    if_id_ = Latch_IF_ID{};
    id_ex_ = Latch_ID_EX{};
    ex_mem_ = Latch_EX_MEM{};
    mem_wb_ = Latch_MEM_WB{};

    if1_if2_ = Latch_IF1_IF2{};
    if2_of_  = Latch_IF2_OF{};
    of_ex_   = Latch_OF_EX{};
    ex_ma_   = Latch_EX_MA{};
    ma_rw_   = Latch_MA_RW{};

    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;
}

void CPUPipeline::loadProgram(const std::vector<word_t>& words, addr_t text_start) {
    reset();
    text_start_ = text_start;
    text_end_ = text_start + static_cast<addr_t>(words.size() * 4);
    pc_ = text_start;

    for (size_t i = 0; i < words.size() && (text_start / 4 + i) < mem_.size(); ++i) {
        mem_[(text_start / 4) + i] = words[i];
    }

    cu_.loadProgram(words, text_start);
}

void CPUPipeline::setPipelineMode(PipelineMode mode) {
    mode_ = mode;
    reset();
}

word_t CPUPipeline::getRegister(reg_t r) const {
    if (r < NUM_REGISTERS) return regs_[r];
    return 0;
}

void CPUPipeline::setRegister(reg_t r, word_t val) {
    if (r < NUM_REGISTERS) {
        regs_[r] = val;
        if (r == SP) stack_engine_.setSP(val);
    }
}

word_t CPUPipeline::readMemory(addr_t addr) const {
    size_t word_idx = addr / 4;
    if (word_idx < mem_.size()) return mem_[word_idx];
    return 0;
}

void CPUPipeline::writeMemory(addr_t addr, word_t val) {
    size_t word_idx = addr / 4;
    if (word_idx < mem_.size()) mem_[word_idx] = val;
}

std::pair<bool, addr_t> CPUPipeline::queryBranchPredictor(addr_t pc) const {
    size_t idx = (pc / 4) % BTB_SIZE;
    const auto& entry = btb_[idx];
    if (entry.valid) {
        bool predict_taken = (entry.state == BranchPredictionState::WEAKLY_TAKEN ||
                              entry.state == BranchPredictionState::STRONGLY_TAKEN);
        return {predict_taken, entry.target};
    }
    return {false, pc + 4};
}

void CPUPipeline::updateBranchPredictor(addr_t pc, bool taken, addr_t target) {
    size_t idx = (pc / 4) % BTB_SIZE;
    auto& entry = btb_[idx];
    entry.valid = true;
    entry.target = target;
    if (taken) {
        if (entry.state == BranchPredictionState::STRONGLY_NOT_TAKEN)
            entry.state = BranchPredictionState::WEAKLY_NOT_TAKEN;
        else if (entry.state == BranchPredictionState::WEAKLY_NOT_TAKEN)
            entry.state = BranchPredictionState::WEAKLY_TAKEN;
        else
            entry.state = BranchPredictionState::STRONGLY_TAKEN;
    } else {
        if (entry.state == BranchPredictionState::STRONGLY_TAKEN)
            entry.state = BranchPredictionState::WEAKLY_TAKEN;
        else if (entry.state == BranchPredictionState::WEAKLY_TAKEN)
            entry.state = BranchPredictionState::WEAKLY_NOT_TAKEN;
        else
            entry.state = BranchPredictionState::STRONGLY_NOT_TAKEN;
    }
}

bool CPUPipeline::is4StageEmpty() const {
    return !if_id_.valid && !id_ex_.valid && !ex_mem_.valid && !mem_wb_.valid;
}

bool CPUPipeline::is6StageEmpty() const {
    return !if1_if2_.valid && !if2_of_.valid && !of_ex_.valid && !ex_ma_.valid && !ma_rw_.valid;
}

// ============================================================================
// 4-STAGE PIPELINE ENGINE (IF -> ID/RR -> EX -> MEM/WB)
// ============================================================================
bool CPUPipeline::step4StageCycle() {
    if (halted_) return false;

    if (pc_ >= text_end_ && is4StageEmpty()) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;

    Latch_MEM_WB next_mem_wb{};
    Latch_EX_MEM next_ex_mem{};
    Latch_ID_EX  next_id_ex{};
    Latch_IF_ID  next_if_id{};

    bool stall_pipeline = false;
    bool branch_taken = false;
    addr_t branch_target = 0;
    bool branch_resolved_in_id = false;

    // ------------------------------------------------------------------------
    // STAGE 4: MEMORY ACCESS & WRITEBACK (MEM/WB) WITH L1 D-CACHE & STORE BUFFER
    // ------------------------------------------------------------------------
    if (ex_mem_.valid && !ex_mem_.bubble) {
        word_t write_data = ex_mem_.alu_result;

        if (ex_mem_.inst.isLoad()) {
            // Check Store Buffer for Store-to-Load Forwarding
            word_t fwd_val = 0;
            if (store_buffer_.lookup(ex_mem_.alu_result, fwd_val)) {
                write_data = fwd_val;
                metrics_.store_load_forwards++;
            } else {
                metrics_.dcache_accesses++;
                bool dhit = dcache_.access(ex_mem_.alu_result, false, 0, mem_, write_data);
                if (dhit) metrics_.dcache_hits++;
            }
        } else if (ex_mem_.inst.isStore()) {
            // Insert into Store Buffer and write-through to D-Cache and memory
            store_buffer_.insert(ex_mem_.alu_result, ex_mem_.store_data);
            metrics_.dcache_accesses++;
            word_t dummy = 0;
            bool dhit = dcache_.access(ex_mem_.alu_result, true, ex_mem_.store_data, mem_, dummy);
            if (dhit) metrics_.dcache_hits++;
            writeMemory(ex_mem_.alu_result, ex_mem_.store_data);
        }

        if (ex_mem_.inst.opcode == OP_CALL) {
            regs_[RA] = ex_mem_.pc + 4;
        } else if (ex_mem_.inst.writesRd()) {
            regs_[ex_mem_.inst.rd] = write_data;
        }

        metrics_.instructions_retired++;

        next_mem_wb.pc = ex_mem_.pc;
        next_mem_wb.inst = ex_mem_.inst;
        next_mem_wb.write_data = write_data;
        next_mem_wb.valid = true;
        next_mem_wb.bubble = false;
        next_mem_wb.inst_id = ex_mem_.inst_id;
    }

    // ------------------------------------------------------------------------
    // STAGE 3: EXECUTE (EX) WITH FORWARDING & BRANCH RESOLUTION
    // ------------------------------------------------------------------------
    if (id_ex_.valid && !id_ex_.bubble) {
        word_t val_a = id_ex_.val_rs1;
        word_t val_b = id_ex_.val_rs2;
        word_t store_data = id_ex_.val_rd;

        // Data Forwarding for Operand A (rs1)
        if (id_ex_.inst.readsRs1()) {
            if (ex_mem_.valid && !ex_mem_.bubble && (ex_mem_.inst.writesRd() || ex_mem_.inst.opcode == OP_CALL) && (ex_mem_.inst.opcode == OP_CALL ? RA : ex_mem_.inst.rd) == id_ex_.inst.rs1) {
                val_a = (ex_mem_.inst.opcode == OP_CALL) ? (ex_mem_.pc + 4) : (ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result);
                last_fwd_a_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && (mem_wb_.inst.writesRd() || mem_wb_.inst.opcode == OP_CALL) && (mem_wb_.inst.opcode == OP_CALL ? RA : mem_wb_.inst.rd) == id_ex_.inst.rs1) {
                val_a = (mem_wb_.inst.opcode == OP_CALL) ? (mem_wb_.pc + 4) : mem_wb_.write_data;
                last_fwd_a_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Operand B (rs2)
        if (id_ex_.inst.readsRs2()) {
            if (ex_mem_.valid && !ex_mem_.bubble && (ex_mem_.inst.writesRd() || ex_mem_.inst.opcode == OP_CALL) && (ex_mem_.inst.opcode == OP_CALL ? RA : ex_mem_.inst.rd) == id_ex_.inst.rs2) {
                val_b = (ex_mem_.inst.opcode == OP_CALL) ? (ex_mem_.pc + 4) : (ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result);
                last_fwd_b_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && (mem_wb_.inst.writesRd() || mem_wb_.inst.opcode == OP_CALL) && (mem_wb_.inst.opcode == OP_CALL ? RA : mem_wb_.inst.rd) == id_ex_.inst.rs2) {
                val_b = (mem_wb_.inst.opcode == OP_CALL) ? (mem_wb_.pc + 4) : mem_wb_.write_data;
                last_fwd_b_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Store Data (rd)
        if (id_ex_.inst.readsStoreRd()) {
            if (ex_mem_.valid && !ex_mem_.bubble && (ex_mem_.inst.writesRd() || ex_mem_.inst.opcode == OP_CALL) && (ex_mem_.inst.opcode == OP_CALL ? RA : ex_mem_.inst.rd) == id_ex_.inst.rd) {
                store_data = (ex_mem_.inst.opcode == OP_CALL) ? (ex_mem_.pc + 4) : (ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result);
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && (mem_wb_.inst.writesRd() || mem_wb_.inst.opcode == OP_CALL) && (mem_wb_.inst.opcode == OP_CALL ? RA : mem_wb_.inst.rd) == id_ex_.inst.rd) {
                store_data = (mem_wb_.inst.opcode == OP_CALL) ? (mem_wb_.pc + 4) : mem_wb_.write_data;
                metrics_.forwarding_events++;
            }
        }

        if (id_ex_.inst.I) {
            val_b = id_ex_.inst.immx;
        }

        // ALU Execution
        word_t alu_out = 0;
        if (id_ex_.inst.isLoad() || id_ex_.inst.isStore()) {
            alu_out = val_a + id_ex_.inst.immx;
        } else if (id_ex_.inst.opcode == OP_MOV) {
            alu_out = val_b;
        } else if (id_ex_.inst.opcode == OP_NOT) {
            alu_out = ~val_b;
        } else if (id_ex_.inst.opcode == OP_CMP) {
            auto res = ALU::execute(ALUOp::CMP, val_a, val_b, flags_);
            flags_.E = res.flags.E;
            flags_.GT = res.flags.GT;
            alu_out = res.result;
        } else if (id_ex_.inst.opcode == OP_NOP) {
            alu_out = 0;
        } else {
            try {
                ALUOp aop = ALU::fromOpcode(id_ex_.inst.opcode);
                auto res = ALU::execute(aop, val_a, val_b, flags_);
                alu_out = res.result;
            } catch (const DivisionByZeroException&) {
                throw DivisionByZeroException(id_ex_.pc);
            }
        }

        // Branch Resolution (Conditional Branches & RET evaluated with ALU flags / RA)
        if (id_ex_.inst.opcode == OP_BEQ || id_ex_.inst.opcode == OP_BGT) {
            bool cond_met = (id_ex_.inst.opcode == OP_BEQ) ? flags_.E : flags_.GT;
            addr_t actual_target = id_ex_.inst.branch_target;
            metrics_.branch_predictions++;
            updateBranchPredictor(id_ex_.pc, cond_met, actual_target);

            if (cond_met == id_ex_.predicted_taken && (!cond_met || id_ex_.predicted_target == actual_target)) {
                // Correct prediction! 0 penalty cycles
                metrics_.branch_prediction_hits++;
            } else {
                // Misprediction penalty
                branch_taken = true;
                branch_target = cond_met ? actual_target : id_ex_.fallthrough_pc;
                last_flush_ = true;
                metrics_.branch_flushes++;
                pc_ = branch_target;
            }
        } else if (id_ex_.inst.opcode == OP_RET) {
            addr_t actual_target = regs_[RA];
            if (ex_mem_.valid && !ex_mem_.bubble && (ex_mem_.inst.writesRd() || ex_mem_.inst.opcode == OP_CALL) && (ex_mem_.inst.opcode == OP_CALL ? RA : ex_mem_.inst.rd) == RA) {
                actual_target = (ex_mem_.inst.opcode == OP_CALL) ? (ex_mem_.pc + 4) : (ex_mem_.inst.isLoad() ? readMemory(ex_mem_.alu_result) : ex_mem_.alu_result);
                metrics_.forwarding_events++;
            } else if (mem_wb_.valid && !mem_wb_.bubble && (mem_wb_.inst.writesRd() || mem_wb_.inst.opcode == OP_CALL) && (mem_wb_.inst.opcode == OP_CALL ? RA : mem_wb_.inst.rd) == RA) {
                actual_target = (mem_wb_.inst.opcode == OP_CALL) ? (mem_wb_.pc + 4) : mem_wb_.write_data;
                metrics_.forwarding_events++;
            }
            metrics_.branch_predictions++;
            if (id_ex_.ras_predicted && id_ex_.predicted_target == actual_target) {
                // RAS Return Prediction Hit! 0 Penalty Bubbles!
                metrics_.ras_hits++;
                metrics_.branch_prediction_hits++;
                branch_taken = false;
                last_flush_ = false;
            } else {
                // RAS Miss or unpredicted return
                branch_taken = true;
                branch_target = actual_target;
                last_flush_ = true;
                metrics_.branch_flushes++;
                pc_ = branch_target;
            }
        }

        next_ex_mem.pc = id_ex_.pc;
        next_ex_mem.inst = id_ex_.inst;
        next_ex_mem.alu_result = alu_out;
        next_ex_mem.store_data = store_data;
        next_ex_mem.branch_taken = branch_taken;
        next_ex_mem.branch_target = branch_target;
        next_ex_mem.fwd_a = last_fwd_a_;
        next_ex_mem.fwd_b = last_fwd_b_;
        next_ex_mem.valid = true;
        next_ex_mem.bubble = false;
        next_ex_mem.inst_id = id_ex_.inst_id;
    }

    // ------------------------------------------------------------------------
    // STAGE 2: INSTRUCTION DECODE, REGISTER READ & HAZARD DETECTION (ID/RR)
    // ------------------------------------------------------------------------
    if (branch_taken) {
        next_id_ex.valid = false;
        next_id_ex.bubble = true;
    } else if (if_id_.valid && !if_id_.bubble) {
        Instruction dec = Instruction::decode(if_id_.raw_inst, if_id_.pc);

        // Load-Use Hazard Detection:
        bool load_use_hazard = false;
        if (id_ex_.valid && !id_ex_.bubble && id_ex_.inst.isLoad()) {
            reg_t load_rd = id_ex_.inst.rd;
            if (dec.readsRs1() && (dec.rs1 == load_rd)) load_use_hazard = true;
            if (dec.readsRs2() && (dec.rs2 == load_rd)) load_use_hazard = true;
            if (dec.readsStoreRd() && (dec.rd == load_rd)) load_use_hazard = true;
            if (dec.readsRA() && (load_rd == RA)) load_use_hazard = true;
        }

        if (load_use_hazard) {
            stall_pipeline = true;
            last_stall_ = true;
            metrics_.stall_cycles++;
            metrics_.load_use_hazards++;

            next_id_ex.valid = false;
            next_id_ex.bubble = true;
        } else {
            word_t val_rs1 = regs_[dec.rs1];
            word_t val_rs2 = regs_[dec.rs2];
            word_t val_rd  = regs_[dec.rd];

            // Internal Writeback Forwarding (write-before-read)
            if (next_mem_wb.valid && !next_mem_wb.bubble) {
                reg_t wr_reg = (next_mem_wb.inst.opcode == OP_CALL) ? RA : next_mem_wb.inst.rd;
                word_t wr_val = (next_mem_wb.inst.opcode == OP_CALL) ? (next_mem_wb.pc + 4) : next_mem_wb.write_data;
                if (next_mem_wb.inst.writesRd() || next_mem_wb.inst.opcode == OP_CALL) {
                    if (wr_reg == dec.rs1) val_rs1 = wr_val;
                    if (wr_reg == dec.rs2) val_rs2 = wr_val;
                    if (wr_reg == dec.rd)  val_rd  = wr_val;
                }
            }

            // Front-End Hardware Stack Engine:
            // Absorb SP additions/subtractions in ID, updating regs_[SP] immediately.
            // Eliminates ALU contention and avoids RAW hazard delay for subsequent SP accesses.
            if (StackEngine::isSpAdjustment(dec)) {
                word_t new_sp = stack_engine_.updateInDecode(dec, val_rs1);
                regs_[SP] = new_sp;
                val_rd = new_sp;
                dec.opcode = OP_NOP; // Absorbed in front-end; ALU freed from pointer arithmetic
                metrics_.stack_engine_resolutions++;
            }

            // Early Branch Resolution in ID for Unconditional Branches (B, CALL)
            if (dec.opcode == OP_CALL) {
                ras_.push(if_id_.pc + 4);
            }

            if (dec.opcode == OP_B || dec.opcode == OP_CALL) {
                addr_t target = dec.branch_target;
                metrics_.branch_predictions++;
                updateBranchPredictor(if_id_.pc, true, target);

                if (!if_id_.predicted_taken || if_id_.predicted_target != target) {
                    branch_resolved_in_id = true;
                    last_flush_ = true;
                    metrics_.branch_flushes++;
                    metrics_.early_branch_resolutions++;
                    pc_ = target;
                } else {
                    metrics_.branch_prediction_hits++;
                }
            }

            next_id_ex.pc = if_id_.pc;
            next_id_ex.inst = dec;
            next_id_ex.val_rs1 = val_rs1;
            next_id_ex.val_rs2 = val_rs2;
            next_id_ex.val_rd = val_rd;
            next_id_ex.predicted_taken = if_id_.predicted_taken;
            next_id_ex.predicted_target = if_id_.predicted_target;
            next_id_ex.fallthrough_pc = if_id_.fallthrough_pc;
            next_id_ex.ras_predicted = if_id_.ras_predicted;
            next_id_ex.valid = true;
            next_id_ex.bubble = false;
            next_id_ex.inst_id = if_id_.inst_id;
        }
    }

    // ------------------------------------------------------------------------
    // STAGE 1: INSTRUCTION FETCH (IF) WITH DYNAMIC BRANCH PREDICTION
    // ------------------------------------------------------------------------
    if (branch_taken || branch_resolved_in_id) {
        next_if_id.valid = false;
        next_if_id.bubble = true;
    } else if (stall_pipeline) {
        next_if_id = if_id_;
    } else {
        if (pc_ < text_end_) {
            // L1 Instruction Cache Access
            metrics_.icache_accesses++;
            word_t raw = 0;
            bool ihit = icache_.access(pc_, false, 0, mem_, raw);
            if (ihit) metrics_.icache_hits++;

            // Check if memory structural hazard was avoided (both IF and MEM active)
            if (ex_mem_.valid && !ex_mem_.bubble && (ex_mem_.inst.isLoad() || ex_mem_.inst.isStore())) {
                metrics_.structural_stalls_avoided++;
            }

            next_if_id.pc = pc_;
            next_if_id.raw_inst = raw;
            next_if_id.valid = true;
            next_if_id.bubble = false;
            next_if_id.inst_id = ++next_inst_id_;

            // Speculative Return Address Stack (RAS) Check for 'ret'
            // In SimpleRISC, OP_RET is opcode 20 (0b10100) in bits [31:27]
            Opcode op = static_cast<Opcode>((raw >> 27) & 0x1F);
            if (op == OP_RET && !ras_.empty()) {
                addr_t pred_ret = ras_.pop();
                next_if_id.ras_predicted = true;
                next_if_id.predicted_target = pred_ret;
                next_if_id.predicted_taken = true;
                next_if_id.fallthrough_pc = pc_ + 4;
                metrics_.ras_predictions++;
                pc_ = pred_ret;
            } else {
                auto pred = queryBranchPredictor(pc_);
                if (pred.first && pred.second < text_end_) {
                    next_if_id.predicted_taken = true;
                    next_if_id.predicted_target = pred.second;
                    next_if_id.fallthrough_pc = pc_ + 4;
                    pc_ = pred.second;
                } else {
                    next_if_id.predicted_taken = false;
                    next_if_id.fallthrough_pc = pc_ + 4;
                    pc_ += 4;
                }
            }
        } else {
            next_if_id.valid = false;
            next_if_id.bubble = true;
        }
    }

    mem_wb_ = next_mem_wb;
    ex_mem_ = next_ex_mem;
    id_ex_  = next_id_ex;
    if_id_  = next_if_id;

    recordCycleSnapshot();
    return true;
}

// ============================================================================
// 6-STAGE SUPER-PIPELINED ENGINE (IF1 -> IF2 -> ID -> RR -> EX -> WB)
// ============================================================================
bool CPUPipeline::step6StageCycle() {
    if (halted_) return false;

    if (pc_ >= text_end_ && is6StageEmpty()) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    last_stall_ = false;
    last_flush_ = false;
    last_fwd_a_ = ForwardSource::NONE;
    last_fwd_b_ = ForwardSource::NONE;

    Latch_MA_RW  next_ma_rw{};
    Latch_EX_MA  next_ex_ma{};
    Latch_OF_EX  next_of_ex{};
    Latch_IF2_OF next_if2_of{};
    Latch_IF1_IF2 next_if1_if2{};

    bool stall_6stage = false;
    bool branch_taken = false;
    addr_t branch_target = 0;
    bool branch_resolved_in_of = false;

    // ------------------------------------------------------------------------
    // STAGE 6: REGISTER WRITEBACK (RW)
    // ------------------------------------------------------------------------
    if (ma_rw_.valid && !ma_rw_.bubble) {
        if (ma_rw_.inst.opcode == OP_CALL) {
            regs_[RA] = ma_rw_.pc + 4;
        } else if (ma_rw_.inst.writesRd()) {
            regs_[ma_rw_.inst.rd] = ma_rw_.write_data;
        }

        metrics_.instructions_retired++;
    }

    // ------------------------------------------------------------------------
    // STAGE 5: DATA MEMORY ACCESS (MA) WITH D-CACHE & STORE BUFFER
    // ------------------------------------------------------------------------
    if (ex_ma_.valid && !ex_ma_.bubble) {
        word_t write_data = ex_ma_.alu_result;
        if (ex_ma_.inst.isLoad()) {
            word_t fwd = 0;
            if (store_buffer_.lookup(ex_ma_.alu_result, fwd)) {
                write_data = fwd;
                metrics_.store_load_forwards++;
            } else {
                metrics_.dcache_accesses++;
                bool dhit = dcache_.access(ex_ma_.alu_result, false, 0, mem_, write_data);
                if (dhit) metrics_.dcache_hits++;
            }
        } else if (ex_ma_.inst.isStore()) {
            store_buffer_.insert(ex_ma_.alu_result, ex_ma_.store_data);
            metrics_.dcache_accesses++;
            word_t dummy = 0;
            bool dhit = dcache_.access(ex_ma_.alu_result, true, ex_ma_.store_data, mem_, dummy);
            if (dhit) metrics_.dcache_hits++;
            writeMemory(ex_ma_.alu_result, ex_ma_.store_data);
        }

        next_ma_rw.pc = ex_ma_.pc;
        next_ma_rw.inst = ex_ma_.inst;
        next_ma_rw.write_data = write_data;
        next_ma_rw.valid = true;
        next_ma_rw.bubble = false;
        next_ma_rw.inst_id = ex_ma_.inst_id;
    }

    // ------------------------------------------------------------------------
    // STAGE 4: EXECUTE (EX) WITH ALU, FORWARDING & BRANCH RESOLUTION
    // ------------------------------------------------------------------------
    if (of_ex_.valid && !of_ex_.bubble) {
        word_t val_a = of_ex_.val_rs1;
        word_t val_b = of_ex_.val_rs2;
        word_t store_data = of_ex_.val_rd;

        // Data Forwarding for Operand A (rs1)
        if (of_ex_.inst.readsRs1()) {
            if (ex_ma_.valid && !ex_ma_.bubble && !ex_ma_.inst.isLoad() && (ex_ma_.inst.writesRd() || ex_ma_.inst.opcode == OP_CALL) && (ex_ma_.inst.opcode == OP_CALL ? RA : ex_ma_.inst.rd) == of_ex_.inst.rs1) {
                val_a = (ex_ma_.inst.opcode == OP_CALL) ? (ex_ma_.pc + 4) : ex_ma_.alu_result;
                last_fwd_a_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (ma_rw_.valid && !ma_rw_.bubble && (ma_rw_.inst.writesRd() || ma_rw_.inst.opcode == OP_CALL) && (ma_rw_.inst.opcode == OP_CALL ? RA : ma_rw_.inst.rd) == of_ex_.inst.rs1) {
                val_a = (ma_rw_.inst.opcode == OP_CALL) ? (ma_rw_.pc + 4) : ma_rw_.write_data;
                last_fwd_a_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Operand B (rs2)
        if (of_ex_.inst.readsRs2()) {
            if (ex_ma_.valid && !ex_ma_.bubble && !ex_ma_.inst.isLoad() && (ex_ma_.inst.writesRd() || ex_ma_.inst.opcode == OP_CALL) && (ex_ma_.inst.opcode == OP_CALL ? RA : ex_ma_.inst.rd) == of_ex_.inst.rs2) {
                val_b = (ex_ma_.inst.opcode == OP_CALL) ? (ex_ma_.pc + 4) : ex_ma_.alu_result;
                last_fwd_b_ = ForwardSource::EX_MEM;
                metrics_.forwarding_events++;
            } else if (ma_rw_.valid && !ma_rw_.bubble && (ma_rw_.inst.writesRd() || ma_rw_.inst.opcode == OP_CALL) && (ma_rw_.inst.opcode == OP_CALL ? RA : ma_rw_.inst.rd) == of_ex_.inst.rs2) {
                val_b = (ma_rw_.inst.opcode == OP_CALL) ? (ma_rw_.pc + 4) : ma_rw_.write_data;
                last_fwd_b_ = ForwardSource::MEM_WB;
                metrics_.forwarding_events++;
            }
        }

        // Data Forwarding for Store Data (rd)
        if (of_ex_.inst.readsStoreRd()) {
            if (ex_ma_.valid && !ex_ma_.bubble && !ex_ma_.inst.isLoad() && (ex_ma_.inst.writesRd() || ex_ma_.inst.opcode == OP_CALL) && (ex_ma_.inst.opcode == OP_CALL ? RA : ex_ma_.inst.rd) == of_ex_.inst.rd) {
                store_data = (ex_ma_.inst.opcode == OP_CALL) ? (ex_ma_.pc + 4) : ex_ma_.alu_result;
                metrics_.forwarding_events++;
            } else if (ma_rw_.valid && !ma_rw_.bubble && (ma_rw_.inst.writesRd() || ma_rw_.inst.opcode == OP_CALL) && (ma_rw_.inst.opcode == OP_CALL ? RA : ma_rw_.inst.rd) == of_ex_.inst.rd) {
                store_data = (ma_rw_.inst.opcode == OP_CALL) ? (ma_rw_.pc + 4) : ma_rw_.write_data;
                metrics_.forwarding_events++;
            }
        }

        if (of_ex_.inst.I) val_b = of_ex_.inst.immx;

        word_t alu_out = 0;
        if (of_ex_.inst.isLoad() || of_ex_.inst.isStore()) {
            alu_out = val_a + of_ex_.inst.immx;
        } else if (of_ex_.inst.opcode == OP_MOV) {
            alu_out = val_b;
        } else if (of_ex_.inst.opcode == OP_NOT) {
            alu_out = ~val_b;
        } else if (of_ex_.inst.opcode == OP_CMP) {
            auto res = ALU::execute(ALUOp::CMP, val_a, val_b, flags_);
            flags_.E = res.flags.E;
            flags_.GT = res.flags.GT;
            alu_out = res.result;
        } else if (of_ex_.inst.opcode == OP_NOP) {
            alu_out = 0;
        } else {
            try {
                ALUOp aop = ALU::fromOpcode(of_ex_.inst.opcode);
                auto res = ALU::execute(aop, val_a, val_b, flags_);
                alu_out = res.result;
            } catch (const DivisionByZeroException&) {
                throw DivisionByZeroException(of_ex_.pc);
            }
        }

        branch_target = of_ex_.inst.branch_target;
        if (of_ex_.inst.opcode == OP_BEQ || of_ex_.inst.opcode == OP_BGT) {
            bool cond_met = (of_ex_.inst.opcode == OP_BEQ) ? flags_.E : flags_.GT;
            addr_t actual_target = of_ex_.inst.branch_target;
            metrics_.branch_predictions++;
            updateBranchPredictor(of_ex_.pc, cond_met, actual_target);

            if (cond_met == of_ex_.predicted_taken && (!cond_met || of_ex_.predicted_target == actual_target)) {
                // Correct branch prediction! 0 penalty bubbles
                metrics_.branch_prediction_hits++;
                branch_taken = false;
            } else {
                // Misprediction penalty
                branch_taken = true;
                branch_target = cond_met ? actual_target : of_ex_.fallthrough_pc;
                last_flush_ = true;
                metrics_.branch_flushes++;
                pc_ = branch_target;
            }
        } else if (of_ex_.inst.opcode == OP_RET) {
            addr_t actual_target = regs_[RA];
            if (ex_ma_.valid && !ex_ma_.bubble && (ex_ma_.inst.writesRd() || ex_ma_.inst.opcode == OP_CALL) && (ex_ma_.inst.opcode == OP_CALL ? RA : ex_ma_.inst.rd) == RA) {
                actual_target = (ex_ma_.inst.opcode == OP_CALL) ? (ex_ma_.pc + 4) : (ex_ma_.inst.isLoad() ? readMemory(ex_ma_.alu_result) : ex_ma_.alu_result);
                metrics_.forwarding_events++;
            } else if (ma_rw_.valid && !ma_rw_.bubble && (ma_rw_.inst.writesRd() || ma_rw_.inst.opcode == OP_CALL) && (ma_rw_.inst.opcode == OP_CALL ? RA : ma_rw_.inst.rd) == RA) {
                actual_target = (ma_rw_.inst.opcode == OP_CALL) ? (ma_rw_.pc + 4) : ma_rw_.write_data;
                metrics_.forwarding_events++;
            }
            metrics_.branch_predictions++;
            if (of_ex_.ras_predicted && of_ex_.predicted_target == actual_target) {
                // RAS Return Prediction Hit! 0 Penalty Bubbles!
                metrics_.ras_hits++;
                metrics_.branch_prediction_hits++;
                branch_taken = false;
                last_flush_ = false;
            } else {
                // RAS Miss or unpredicted return
                branch_taken = true;
                branch_target = actual_target;
                last_flush_ = true;
                metrics_.branch_flushes++;
                pc_ = branch_target;
            }
        }

        next_ex_ma.pc = of_ex_.pc;
        next_ex_ma.inst = of_ex_.inst;
        next_ex_ma.alu_result = alu_out;
        next_ex_ma.store_data = store_data;
        next_ex_ma.branch_taken = branch_taken;
        next_ex_ma.branch_target = branch_target;
        next_ex_ma.fwd_a = last_fwd_a_;
        next_ex_ma.fwd_b = last_fwd_b_;
        next_ex_ma.valid = true;
        next_ex_ma.bubble = false;
        next_ex_ma.inst_id = of_ex_.inst_id;
    }

    // ------------------------------------------------------------------------
    // STAGE 3: OPERAND FETCH (OF: INSTRUCTION DECODE & REGISTER READ)
    // ------------------------------------------------------------------------
    if (branch_taken) {
        next_of_ex.valid = false;
        next_of_ex.bubble = true;
    } else if (if2_of_.valid && !if2_of_.bubble) {
        Instruction dec = Instruction::decode(if2_of_.raw_inst, if2_of_.pc);

        // Load-Use Hazard Detection:
        // If preceding instruction in EX is a load, its value won't be ready until the end of MA stage.
        // Therefore, any dependent instruction in OF MUST stall 1 cycle.
        bool load_use = false;
        if (of_ex_.valid && !of_ex_.bubble && of_ex_.inst.isLoad()) {
            reg_t lrd = of_ex_.inst.rd;
            if ((dec.readsRs1() && dec.rs1 == lrd) ||
                (dec.readsRs2() && dec.rs2 == lrd) ||
                (dec.readsStoreRd() && dec.rd == lrd) ||
                (dec.readsRA() && lrd == RA)) {
                load_use = true;
            }
        }

        if (load_use) {
            stall_6stage = true;
            last_stall_ = true;
            metrics_.stall_cycles++;
            metrics_.load_use_hazards++;

            next_of_ex.valid = false;
            next_of_ex.bubble = true;
        } else {
            word_t val_rs1 = regs_[dec.rs1];
            word_t val_rs2 = regs_[dec.rs2];
            word_t val_rd  = regs_[dec.rd];

            // Internal Writeback Forwarding (write-before-read from Stage 6 RW):
            if (ma_rw_.valid && !ma_rw_.bubble) {
                reg_t wr_reg = (ma_rw_.inst.opcode == OP_CALL) ? RA : ma_rw_.inst.rd;
                word_t wr_val = (ma_rw_.inst.opcode == OP_CALL) ? (ma_rw_.pc + 4) : ma_rw_.write_data;
                if (ma_rw_.inst.writesRd() || ma_rw_.inst.opcode == OP_CALL) {
                    if (wr_reg == dec.rs1) val_rs1 = wr_val;
                    if (wr_reg == dec.rs2) val_rs2 = wr_val;
                    if (wr_reg == dec.rd)  val_rd  = wr_val;
                }
            }

            // Front-End Hardware Stack Engine (6-Stage Pipeline):
            if (StackEngine::isSpAdjustment(dec)) {
                word_t new_sp = stack_engine_.updateInDecode(dec, val_rs1);
                regs_[SP] = new_sp;
                val_rd = new_sp;
                dec.opcode = OP_NOP; // Absorbed in front-end; ALU freed from pointer arithmetic
                metrics_.stack_engine_resolutions++;
            }

            // Early Branch Resolution in OF for Unconditional Branches (B, CALL)
            if (dec.opcode == OP_CALL) {
                ras_.push(if2_of_.pc + 4);
            }

            if (dec.opcode == OP_B || dec.opcode == OP_CALL) {
                addr_t target = dec.branch_target;
                metrics_.branch_predictions++;
                updateBranchPredictor(if2_of_.pc, true, target);

                if (!if2_of_.predicted_taken || if2_of_.predicted_target != target) {
                    branch_resolved_in_of = true;
                    last_flush_ = true;
                    metrics_.branch_flushes++;
                    metrics_.early_branch_resolutions++;
                    pc_ = target;
                } else {
                    metrics_.branch_prediction_hits++;
                }
            }

            next_of_ex.pc = if2_of_.pc;
            next_of_ex.inst = dec;
            next_of_ex.val_rs1 = val_rs1;
            next_of_ex.val_rs2 = val_rs2;
            next_of_ex.val_rd = val_rd;
            next_of_ex.predicted_taken = if2_of_.predicted_taken;
            next_of_ex.predicted_target = if2_of_.predicted_target;
            next_of_ex.fallthrough_pc = if2_of_.fallthrough_pc;
            next_of_ex.ras_predicted = if2_of_.ras_predicted;
            next_of_ex.valid = true;
            next_of_ex.bubble = false;
            next_of_ex.inst_id = if2_of_.inst_id;
        }
    }

    // ------------------------------------------------------------------------
    // STAGE 2: INSTRUCTION FETCH 2 (IF2: MEMORY / I-CACHE ACCESS)
    // ------------------------------------------------------------------------
    if (branch_taken || branch_resolved_in_of) {
        next_if2_of.valid = false;
        next_if2_of.bubble = true;
    } else if (stall_6stage) {
        next_if2_of = if2_of_;
    } else if (if1_if2_.valid && !if1_if2_.bubble) {
        metrics_.icache_accesses++;
        word_t raw = 0;
        bool ihit = icache_.access(if1_if2_.pc, false, 0, mem_, raw);
        if (ihit) metrics_.icache_hits++;

        // Structural Hazard Avoidance (concurrent IF2 and MA memory requests handled by Harvard split cache)
        if (ex_ma_.valid && !ex_ma_.bubble && (ex_ma_.inst.isLoad() || ex_ma_.inst.isStore())) {
            metrics_.structural_stalls_avoided++;
        }

        next_if2_of.pc = if1_if2_.pc;
        next_if2_of.raw_inst = raw;
        next_if2_of.predicted_taken = if1_if2_.predicted_taken;
        next_if2_of.predicted_target = if1_if2_.predicted_target;
        next_if2_of.fallthrough_pc = if1_if2_.fallthrough_pc;
        next_if2_of.ras_predicted = false;
        next_if2_of.valid = true;
        next_if2_of.bubble = false;
        next_if2_of.inst_id = if1_if2_.inst_id;

        // Speculative Return Address Stack (RAS) Check for 'ret'
        Opcode op = static_cast<Opcode>((raw >> 27) & 0x1F);
        if (op == OP_RET && !ras_.empty()) {
            addr_t pred_ret = ras_.pop();
            next_if2_of.ras_predicted = true;
            next_if2_of.predicted_target = pred_ret;
            next_if2_of.predicted_taken = true;
            next_if2_of.fallthrough_pc = if1_if2_.pc + 4;
            pc_ = pred_ret;
        }
    } else {
        next_if2_of.valid = false;
        next_if2_of.bubble = true;
    }

    // ------------------------------------------------------------------------
    // STAGE 1: INSTRUCTION FETCH 1 (IF1: PC GENERATION & ADDRESS SETUP)
    // ------------------------------------------------------------------------
    if (branch_taken || branch_resolved_in_of) {
        next_if1_if2.valid = false;
        next_if1_if2.bubble = true;
    } else if (stall_6stage) {
        next_if1_if2 = if1_if2_;
    } else {
        if (pc_ < text_end_) {
            auto pred = queryBranchPredictor(pc_);
            next_if1_if2.pc = pc_;
            next_if1_if2.predicted_taken = pred.first;
            next_if1_if2.predicted_target = pred.second;
            next_if1_if2.fallthrough_pc = pc_ + 4;
            next_if1_if2.valid = true;
            next_if1_if2.bubble = false;
            next_if1_if2.inst_id = ++next_inst_id_;

            if (pred.first) {
                pc_ = pred.second;
            } else {
                pc_ += 4;
            }
        } else {
            next_if1_if2.valid = false;
            next_if1_if2.bubble = true;
        }
    }

    ma_rw_   = next_ma_rw;
    ex_ma_   = next_ex_ma;
    of_ex_   = next_of_ex;
    if2_of_  = next_if2_of;
    if1_if2_ = next_if1_if2;

    recordCycleSnapshot();
    return true;
}

// ============================================================================
// MICROPROGRAMMED SHARED-BUS ENGINE
// ============================================================================
bool CPUPipeline::stepMicroCycle() {
    if (halted_) return false;

    if (cu_.getPC() >= text_end_ && cu_.getUPC() == 0) {
        halted_ = true;
        return false;
    }

    metrics_.total_cycles++;
    bool done = cu_.stepMicroCycle();
    if (done) metrics_.instructions_retired++;

    // Mirror registers from control unit to pipeline
    for (reg_t i = 0; i < NUM_REGISTERS; ++i) {
        regs_[i] = cu_.getRegister(i);
    }
    pc_ = cu_.getPC();
    flags_ = cu_.getFlags();

    return true;
}

// ============================================================================
// HIGH-LEVEL STEPPING AND RUNNER
// ============================================================================
bool CPUPipeline::cycleStep() {
    if (halted_) return false;
    try {
        switch (mode_) {
            case PipelineMode::FOUR_STAGE:
                return step4StageCycle();
            case PipelineMode::SIX_STAGE:
                return step6StageCycle();
            case PipelineMode::MICROPROGRAMMED:
                return stepMicroCycle();
        }
    } catch (const SimpleRISCException& ex) {
        has_exception_ = true;
        exception_msg_ = ex.what();
        halted_ = true;
        return false;
    } catch (const std::exception& ex) {
        has_exception_ = true;
        exception_msg_ = ex.what();
        halted_ = true;
        return false;
    }
    return false;
}

bool CPUPipeline::macroStep() {
    if (halted_) return false;

    if (mode_ == PipelineMode::MICROPROGRAMMED) {
        uint32_t micro_cycles = cu_.stepInstruction();
        metrics_.total_cycles += micro_cycles;
        metrics_.instructions_retired++;
        for (reg_t i = 0; i < NUM_REGISTERS; ++i) {
            regs_[i] = cu_.getRegister(i);
        }
        pc_ = cu_.getPC();
        flags_ = cu_.getFlags();
        if (pc_ >= text_end_) halted_ = true;
        return true;
    } else {
        uint64_t prev = metrics_.instructions_retired;
        while (!halted_) {
            if (!cycleStep()) break;
            if (metrics_.instructions_retired > prev) return true;
        }
        return !halted_;
    }
}

void CPUPipeline::run(uint64_t max_cycles) {
    while (!halted_ && metrics_.total_cycles < max_cycles) {
        if (!cycleStep()) break;
    }
}

// ============================================================================
// VISUALIZERS & TELEMETRY
// ============================================================================
static std::string fwdStr(ForwardSource f) {
    switch (f) {
        case ForwardSource::EX_MEM: return "EX/MEM";
        case ForwardSource::MEM_WB: return "MEM/WB";
        default: return "NONE";
    }
}

std::vector<PipelineStageSnapshot> CPUPipeline::getStageSnapshots() const {
    std::vector<PipelineStageSnapshot> res;
    if (mode_ == PipelineMode::FOUR_STAGE) {
        // IF
        PipelineStageSnapshot s_if;
        s_if.stage_name = "IF";
        s_if.pc = if_id_.pc;
        s_if.raw = if_id_.raw_inst;
        s_if.valid = if_id_.valid;
        s_if.bubble = if_id_.bubble;
        s_if.inst_id = if_id_.inst_id;
        if (if_id_.valid && !if_id_.bubble) {
            s_if.instruction_text = Disassembler::disassemble(if_id_.raw_inst, if_id_.pc).full_assembly;
            if (if_id_.predicted_taken) {
                s_if.details = "Predicted Taken -> " + toHex8(if_id_.predicted_target);
            }
        } else if (if_id_.bubble) {
            s_if.is_bubble = true;
            s_if.stall_reason = "BRANCH_FLUSH";
        }
        res.push_back(s_if);

        // ID/RR
        PipelineStageSnapshot s_id;
        s_id.stage_name = "ID/RR";
        s_id.pc = id_ex_.pc;
        s_id.raw = id_ex_.inst.raw;
        s_id.valid = id_ex_.valid;
        s_id.bubble = id_ex_.bubble;
        s_id.inst_id = id_ex_.inst_id;
        s_id.is_stalled = last_stall_;
        if (last_stall_) s_id.stall_reason = "RAW_LOAD_USE";
        if (id_ex_.valid && !id_ex_.bubble) {
            s_id.instruction_text = Disassembler::disassemble(id_ex_.inst.raw, id_ex_.pc).full_assembly;
            s_id.details = "Decoded Rd: r" + std::to_string(id_ex_.inst.rd) + ", Rs1: r" + std::to_string(id_ex_.inst.rs1);
        } else if (id_ex_.bubble) {
            s_id.is_bubble = true;
            s_id.stall_reason = last_stall_ ? "RAW_LOAD_USE" : "FLUSH";
        }
        res.push_back(s_id);

        // EX
        PipelineStageSnapshot s_ex;
        s_ex.stage_name = "EX";
        s_ex.pc = ex_mem_.pc;
        s_ex.raw = ex_mem_.inst.raw;
        s_ex.valid = ex_mem_.valid;
        s_ex.bubble = ex_mem_.bubble;
        s_ex.inst_id = ex_mem_.inst_id;
        if (ex_mem_.valid && !ex_mem_.bubble) {
            s_ex.instruction_text = Disassembler::disassemble(ex_mem_.inst.raw, ex_mem_.pc).full_assembly;
            s_ex.details = "ALU Result: " + toHex8(ex_mem_.alu_result);
            if (ex_mem_.fwd_a != ForwardSource::NONE || ex_mem_.fwd_b != ForwardSource::NONE) {
                s_ex.forwarding_info = "Fwd A: " + fwdStr(ex_mem_.fwd_a) + ", B: " + fwdStr(ex_mem_.fwd_b);
            }
        } else if (ex_mem_.bubble) {
            s_ex.is_bubble = true;
        }
        res.push_back(s_ex);

        // MEM/WB
        PipelineStageSnapshot s_wb;
        s_wb.stage_name = "MEM/WB";
        s_wb.pc = mem_wb_.pc;
        s_wb.raw = mem_wb_.inst.raw;
        s_wb.valid = mem_wb_.valid;
        s_wb.bubble = mem_wb_.bubble;
        s_wb.inst_id = mem_wb_.inst_id;
        if (mem_wb_.valid && !mem_wb_.bubble) {
            s_wb.instruction_text = Disassembler::disassemble(mem_wb_.inst.raw, mem_wb_.pc).full_assembly;
            s_wb.details = "Writeback: " + toHex8(mem_wb_.write_data) + " committed";
        } else if (mem_wb_.bubble) {
            s_wb.is_bubble = true;
        }
        res.push_back(s_wb);
    } else if (mode_ == PipelineMode::SIX_STAGE) {
        // IF1
        PipelineStageSnapshot s_if1;
        s_if1.stage_name = "IF1";
        s_if1.pc = pc_;
        s_if1.valid = (pc_ < text_end_);
        s_if1.instruction_text = "(Address Gen)";
        s_if1.details = "Next PC: " + toHex8(pc_);
        res.push_back(s_if1);

        // IF2
        PipelineStageSnapshot s_if2;
        s_if2.stage_name = "IF2";
        s_if2.pc = if1_if2_.pc;
        s_if2.valid = if1_if2_.valid;
        s_if2.bubble = if1_if2_.bubble;
        s_if2.inst_id = if1_if2_.inst_id;
        s_if2.instruction_text = "(Instruction Memory Latch)";
        res.push_back(s_if2);

        // OF
        PipelineStageSnapshot s_of;
        s_of.stage_name = "OF";
        s_of.pc = if2_of_.pc;
        s_of.raw = if2_of_.raw_inst;
        s_of.valid = if2_of_.valid;
        s_of.bubble = if2_of_.bubble;
        s_of.inst_id = if2_of_.inst_id;
        s_of.is_stalled = last_stall_;
        if (last_stall_) s_of.stall_reason = "RAW_LOAD_USE";
        if (if2_of_.valid && !if2_of_.bubble) {
            s_of.instruction_text = Disassembler::disassemble(if2_of_.raw_inst, if2_of_.pc).full_assembly;
        } else if (if2_of_.bubble) {
            s_of.is_bubble = true;
            s_of.stall_reason = last_stall_ ? "RAW_LOAD_USE" : "FLUSH";
        }
        res.push_back(s_of);

        // EX
        PipelineStageSnapshot s_ex;
        s_ex.stage_name = "EX";
        s_ex.pc = of_ex_.pc;
        s_ex.raw = of_ex_.inst.raw;
        s_ex.valid = of_ex_.valid;
        s_ex.bubble = of_ex_.bubble;
        s_ex.inst_id = of_ex_.inst_id;
        if (of_ex_.valid && !of_ex_.bubble) {
            s_ex.instruction_text = Disassembler::disassemble(of_ex_.inst.raw, of_ex_.pc).full_assembly;
            s_ex.details = "ALU Result: " + toHex8(ex_ma_.alu_result);
            if (last_fwd_a_ != ForwardSource::NONE || last_fwd_b_ != ForwardSource::NONE) {
                s_ex.forwarding_info = "Fwd A: " + fwdStr(last_fwd_a_) + ", B: " + fwdStr(last_fwd_b_);
            }
        } else if (of_ex_.bubble) {
            s_ex.is_bubble = true;
        }
        res.push_back(s_ex);

        // MA
        PipelineStageSnapshot s_ma;
        s_ma.stage_name = "MA";
        s_ma.pc = ex_ma_.pc;
        s_ma.raw = ex_ma_.inst.raw;
        s_ma.valid = ex_ma_.valid;
        s_ma.bubble = ex_ma_.bubble;
        s_ma.inst_id = ex_ma_.inst_id;
        if (ex_ma_.valid && !ex_ma_.bubble) {
            s_ma.instruction_text = Disassembler::disassemble(ex_ma_.inst.raw, ex_ma_.pc).full_assembly;
            s_ma.details = "(Data Memory Access)";
        } else if (ex_ma_.bubble) {
            s_ma.is_bubble = true;
        }
        res.push_back(s_ma);

        // RW
        PipelineStageSnapshot s_rw;
        s_rw.stage_name = "RW";
        s_rw.pc = ma_rw_.pc;
        s_rw.raw = ma_rw_.inst.raw;
        s_rw.valid = ma_rw_.valid;
        s_rw.bubble = ma_rw_.bubble;
        s_rw.inst_id = ma_rw_.inst_id;
        if (ma_rw_.valid && !ma_rw_.bubble) {
            s_rw.instruction_text = Disassembler::disassemble(ma_rw_.inst.raw, ma_rw_.pc).full_assembly;
            s_rw.details = "Committed: " + toHex8(ma_rw_.write_data) + " to r" + std::to_string(ma_rw_.inst.rd);
        } else if (ma_rw_.bubble) {
            s_rw.is_bubble = true;
        }
        res.push_back(s_rw);
    }
    return res;
}

void CPUPipeline::recordCycleSnapshot() {
    CycleSnapshot snap;
    snap.cycle = metrics_.total_cycles;
    snap.had_stall = last_stall_;
    snap.had_flush = last_flush_;
    snap.stages = getStageSnapshots();
    cycle_history_.push_back(snap);
}

std::string CPUPipeline::dumpTrace() const {
    std::string mode_str = (mode_ == PipelineMode::SIX_STAGE ? "6-Stage" : "4-Stage");
    return PipelineVisualizer::renderTrace(cycle_history_, mode_str);
}

std::string CPUPipeline::dumpSpaceTimeDiagram() const {
    return PipelineVisualizer::renderSpaceTimeDiagram(cycle_history_);
}

std::string CPUPipeline::dump4StageState() const {
    return PipelineVisualizer::renderStages(
        getStageSnapshots(),
        metrics_.total_cycles,
        "4-Stage",
        last_stall_,
        last_flush_,
        metrics_.getCPI(),
        metrics_.getIPC()
    );
}

std::string CPUPipeline::dump6StageState() const {
    return PipelineVisualizer::renderStages(
        getStageSnapshots(),
        metrics_.total_cycles,
        "6-Stage",
        last_stall_,
        last_flush_,
        metrics_.getCPI(),
        metrics_.getIPC()
    );
}

std::string CPUPipeline::dumpPipelineState() const {
    if (mode_ == PipelineMode::FOUR_STAGE) {
        return dump4StageState();
    } else if (mode_ == PipelineMode::SIX_STAGE) {
        return dump6StageState();
    } else {
        return cu_.dumpInternalRegisters();
    }
}

std::string CPUPipeline::dumpRegisters() const {
    std::ostringstream oss;
    oss << "\n+-----------------------------------------------------------------------------+\n";
    oss << "|                        ARCHITECTURAL REGISTER FILE                          |\n";
    oss << "+-----------------------------------------------------------------------------+\n";

    auto regStr = [](const std::string& name, word_t val) {
        std::ostringstream s;
        s << std::setw(4) << std::left << name << ": 0x"
          << std::right << std::hex << std::uppercase << std::setw(8) << std::setfill('0') << val;
        return s.str(); // 16 chars
    };

    for (size_t row = 0; row < 4; ++row) {
        oss << "|  ";
        for (size_t col = 0; col < 4; ++col) {
            size_t r = row * 4 + col;
            std::string name = (r == SP) ? "sp" : ((r == RA) ? "ra" : ("r" + std::to_string(r)));
            oss << regStr(name, regs_[r]);
            if (col < 3) oss << "   ";
        }
        oss << "  |\n";
    }

    oss << "+-----------------------------------------------------------------------------+\n";
    std::ostringstream s_status;
    s_status << "PC: " << toHex8(pc_) << " | SP: " << toHex8(regs_[SP])
             << " | Flags: [E=" << flags_.E << " GT=" << flags_.GT << "]"
             << " | Retired: " << std::dec << metrics_.instructions_retired;

    oss << "|  " << std::left << std::setw(73) << s_status.str() << " |\n";
    oss << "+-----------------------------------------------------------------------------+\n";
    return oss.str();
}

} // namespace simplerisc
