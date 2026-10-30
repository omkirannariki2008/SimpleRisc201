#include "assembler.hpp"
#include <fstream>
#include <sstream>
#include <algorithm>
#include <cctype>
#include <unordered_map>
#include <set>

namespace simplerisc {

// --- Helper Functions ---
static std::string trim(const std::string& str) {
    size_t first = str.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return "";
    return str.substr(first, str.find_last_not_of(" \t\r\n") - first + 1);
}

static std::string toUpper(std::string str) {
    std::transform(str.begin(), str.end(), str.begin(), ::toupper);
    return str;
}

static std::vector<std::string> splitTokens(const std::string& str) {
    std::vector<std::string> tokens;
    size_t prev = 0, pos;
    while ((pos = str.find_first_of(" ,\t\r\n()[]", prev)) != std::string::npos) {
        if (pos > prev) tokens.push_back(str.substr(prev, pos - prev));
        prev = pos + 1;
    }
    if (prev < str.length()) tokens.push_back(str.substr(prev));
    return tokens;
}

// --- Opcode Lookup Table ---
static const std::unordered_map<std::string, Opcode> opMap = {
    {"NOP", OP_NOP}, {"RET", OP_RET}, {"B", OP_B}, {"BEQ", OP_BEQ}, 
    {"BGT", OP_BGT}, {"CALL", OP_CALL}, {"CMP", OP_CMP}, {"MOV", OP_MOV}, 
    {"NOT", OP_NOT}, {"LD", OP_LD}, {"ST", OP_ST}, {"ADD", OP_ADD}, 
    {"SUB", OP_SUB}, {"MUL", OP_MUL}, {"DIV", OP_DIV}, {"MOD", OP_MOD}, 
    {"AND", OP_AND}, {"OR", OP_OR}, {"LSL", OP_LSL}, {"LSR", OP_LSR}, {"ASR", OP_ASR},
    {"I2D", OP_I2D}, {"D2I", OP_D2I}, {"FDIV", OP_FDIV}
};

// --- Assembler Class Implementation ---
void Assembler::reset() {
    symbol_table_.clear();
}

bool Assembler::parseRegister(const std::string& token, reg_t& out_reg) {
    std::string upper = toUpper(trim(token));
    if (upper == "SP" || upper == "$SP" || upper == "R14" || upper == "$14") { out_reg = 14; return true; }
    if (upper == "RA" || upper == "$RA" || upper == "R15" || upper == "$15") { out_reg = 15; return true; }
    if (upper.size() >= 2 && (upper[0] == 'R' || upper[0] == '$')) {
        try {
            int num = std::stoi(upper.substr(1));
            if (num >= 0 && num <= 15) { out_reg = static_cast<reg_t>(num); return true; }
        } catch (...) {}
    }
    return false;
}

bool Assembler::parseImmediate(const std::string& token, int32_t& out_imm) {
    std::string clean = trim(token);
    if (clean.empty()) return false;
    try {
        if (clean.size() > 2 && (clean.substr(0, 2) == "0x" || clean.substr(0, 2) == "0X")) {
            out_imm = static_cast<int32_t>(std::stoul(clean, nullptr, 16));
        } else {
            out_imm = std::stol(clean, nullptr, 10);
        }
        return true;
    } catch (...) { return false; }
}

AssemblyResult Assembler::assembleFile(const std::string& filepath) {
    std::ifstream file(filepath);
    if (!file.is_open()) return {false, "Could not open file: " + filepath, {}, {}, 0}; 
    std::stringstream buffer;
    buffer << file.rdbuf();
    return assemble(buffer.str());
}

struct Statement {
    std::vector<std::string> labels;
    std::vector<std::string> tokens;
};

struct RegUsage {
    std::vector<reg_t> read_regs;
    std::vector<reg_t> written_regs;
    bool is_load{false};
    bool is_store{false};
    bool is_branch{false};
    bool is_call{false};
    bool is_ret{false};
    bool is_halt{false};
    bool is_cmp{false};
};

AssemblyResult Assembler::assemble(const std::string& raw_source) {
    reset();
    AssemblyResult result;
    result.success = true;

    auto getInstructionRegUsage = [&](const std::vector<std::string>& tokens) -> RegUsage {
        RegUsage u;
        if (tokens.empty()) return u;
        std::string mnem = toUpper(tokens[0]);

        if (mnem == "NOP") {
            return u;
        } else if (mnem == "HALT") {
            u.is_halt = true;
            u.is_branch = true;
        } else if (mnem == "RET") {
            u.is_ret = true;
            u.is_branch = true;
            u.read_regs.push_back(15); // Reads RA
        } else if (mnem == "B" || mnem == "BEQ" || mnem == "BGT") {
            u.is_branch = true;
        } else if (mnem == "CALL") {
            u.is_call = true;
            u.is_branch = true;
            u.written_regs.push_back(15); // Writes RA
        } else if (mnem == "LD") {
            u.is_load = true;
            reg_t r_dest = 0, r_base = 0;
            if (tokens.size() >= 3 && parseRegister(tokens[1], r_dest)) {
                u.written_regs.push_back(r_dest);
                if (tokens.size() >= 4 && parseRegister(tokens[3], r_base)) {
                    u.read_regs.push_back(r_base);
                } else if (parseRegister(tokens[2], r_base)) {
                    u.read_regs.push_back(r_base);
                }
            }
        } else if (mnem == "ST") {
            u.is_store = true;
            reg_t r_src = 0, r_base = 0;
            if (tokens.size() >= 3 && parseRegister(tokens[1], r_src)) {
                u.read_regs.push_back(r_src);
                if (tokens.size() >= 4 && parseRegister(tokens[3], r_base)) {
                    u.read_regs.push_back(r_base);
                } else if (parseRegister(tokens[2], r_base)) {
                    u.read_regs.push_back(r_base);
                }
            }
        } else if (mnem == "I2D" || mnem == "D2I" || mnem == "NOT") {
            reg_t rd = 0, rs1 = 0;
            if (tokens.size() >= 3 && parseRegister(tokens[1], rd)) {
                u.written_regs.push_back(rd);
                if (parseRegister(tokens[2], rs1)) {
                    u.read_regs.push_back(rs1);
                }
            }
        } else if (mnem == "MOV") {
            reg_t rd = 0, rs1 = 0;
            if (tokens.size() >= 3 && parseRegister(tokens[1], rd)) {
                u.written_regs.push_back(rd);
                if (parseRegister(tokens[2], rs1)) {
                    u.read_regs.push_back(rs1);
                }
            }
        } else if (mnem == "CMP") {
            u.is_cmp = true;
            reg_t rs1 = 0, rs2 = 0;
            if (tokens.size() >= 3 && parseRegister(tokens[1], rs1)) {
                u.read_regs.push_back(rs1);
                if (parseRegister(tokens[2], rs2)) {
                    u.read_regs.push_back(rs2);
                }
            }
        } else { // Standard ALU 3-argument
            reg_t rd = 0, rs1 = 0, rs2 = 0;
            if (tokens.size() >= 4 && parseRegister(tokens[1], rd) && parseRegister(tokens[2], rs1)) {
                u.written_regs.push_back(rd);
                u.read_regs.push_back(rs1);
                if (parseRegister(tokens[3], rs2)) {
                    u.read_regs.push_back(rs2);
                }
            }
        }
        return u;
    };

    // PASS 1A: Parse raw lines into structured statements with attached labels
    std::vector<std::string> pending_labels;
    std::vector<Statement> statements;

    std::stringstream ss(raw_source);
    std::string line;
    while (std::getline(ss, line)) {
        line = line.substr(0, line.find(';')); // Strip comments
        
        size_t colon = line.find(':');
        while (colon != std::string::npos) {
            std::string lbl = trim(line.substr(0, colon));
            if (!lbl.empty()) pending_labels.push_back(lbl);
            line = line.substr(colon + 1);
            colon = line.find(':');
        }
        
        auto tokens = splitTokens(line);
        if (!tokens.empty() && tokens[0] != ".text" && tokens[0] != ".data") {
            Statement stmt;
            stmt.labels = std::move(pending_labels);
            stmt.tokens = tokens;
            statements.push_back(stmt);
            pending_labels.clear();
        }
    }

    // PASS 1B: Static Instruction Scheduling (Load-Use Delay Slot Optimization)
    uint32_t load_use_slots_optimized = 0;
    if (optimize_load_use_) {
        for (size_t i = 0; i < statements.size(); ++i) {
            if (statements[i].tokens.empty()) continue;
            RegUsage load_usage = getInstructionRegUsage(statements[i].tokens);
            if (!load_usage.is_load || load_usage.written_regs.empty()) continue;

            reg_t load_rd = load_usage.written_regs[0];

            // Next instruction must exist
            if (i + 1 >= statements.size()) continue;
            if (statements[i + 1].tokens.empty()) continue;

            // Cannot hoist across a label boundary (label is a jump target)
            if (!statements[i + 1].labels.empty()) continue;

            // Check if next instruction consumes load_rd
            RegUsage next_usage = getInstructionRegUsage(statements[i + 1].tokens);
            bool is_consumer = false;
            for (reg_t r : next_usage.read_regs) {
                if (r == load_rd) {
                    is_consumer = true;
                    break;
                }
            }
            if (!is_consumer) continue; // No load-use hazard!

            // Load-Use hazard detected! Search for an independent candidate instruction in this basic block.
            std::set<reg_t> intermediate_reads;
            std::set<reg_t> intermediate_writes;
            bool intermediate_has_cmp = false;

            size_t candidate_idx = 0;
            bool found_candidate = false;

            for (size_t j = i + 1; j < statements.size(); ++j) {
                // Do not cross label boundaries
                if (!statements[j].labels.empty()) break;

                RegUsage cand_usage = getInstructionRegUsage(statements[j].tokens);

                // Do not cross control-flow instructions
                if (cand_usage.is_branch || cand_usage.is_call || cand_usage.is_ret || cand_usage.is_halt) {
                    break;
                }

                if (j == i + 1) {
                    for (reg_t r : cand_usage.read_regs) intermediate_reads.insert(r);
                    for (reg_t r : cand_usage.written_regs) intermediate_writes.insert(r);
                    if (cand_usage.is_cmp) intermediate_has_cmp = true;
                    continue;
                }

                // Candidate must not be a memory operation (avoids aliasing / reordering memory traffic)
                if (cand_usage.is_load || cand_usage.is_store) {
                    for (reg_t r : cand_usage.read_regs) intermediate_reads.insert(r);
                    for (reg_t r : cand_usage.written_regs) intermediate_writes.insert(r);
                    if (cand_usage.is_cmp) intermediate_has_cmp = true;
                    continue;
                }

                // Candidate cannot read or write the load register
                bool conflicts_with_load = false;
                for (reg_t r : cand_usage.read_regs) {
                    if (r == load_rd) { conflicts_with_load = true; break; }
                }
                for (reg_t r : cand_usage.written_regs) {
                    if (r == load_rd) { conflicts_with_load = true; break; }
                }
                if (conflicts_with_load) {
                    for (reg_t r : cand_usage.read_regs) intermediate_reads.insert(r);
                    for (reg_t r : cand_usage.written_regs) intermediate_writes.insert(r);
                    if (cand_usage.is_cmp) intermediate_has_cmp = true;
                    continue;
                }

                // Candidate cannot conflict with intermediate instructions:
                // RAW: candidate reads what an intermediate wrote
                bool conflicts_intermediate = false;
                for (reg_t r : cand_usage.read_regs) {
                    if (intermediate_writes.count(r)) { conflicts_intermediate = true; break; }
                }
                // WAR: candidate writes what an intermediate read
                for (reg_t r : cand_usage.written_regs) {
                    if (intermediate_reads.count(r)) { conflicts_intermediate = true; break; }
                }
                // WAW: candidate writes what an intermediate wrote
                for (reg_t r : cand_usage.written_regs) {
                    if (intermediate_writes.count(r)) { conflicts_intermediate = true; break; }
                }
                if (cand_usage.is_cmp && intermediate_has_cmp) {
                    conflicts_intermediate = true;
                }

                if (!conflicts_intermediate) {
                    candidate_idx = j;
                    found_candidate = true;
                    break;
                }

                for (reg_t r : cand_usage.read_regs) intermediate_reads.insert(r);
                for (reg_t r : cand_usage.written_regs) intermediate_writes.insert(r);
                if (cand_usage.is_cmp) intermediate_has_cmp = true;
            }

            if (found_candidate) {
                // Hoist statements[candidate_idx] to statements[i + 1]
                Statement hoisted = statements[candidate_idx];
                statements.erase(statements.begin() + candidate_idx);
                statements.insert(statements.begin() + i + 1, hoisted);
                load_use_slots_optimized++;
                i++; // Skip past the newly placed instruction
            }
        }
    }
    result.load_use_slots_optimized = load_use_slots_optimized;

    // PASS 1C: Assign PC addresses to symbol table and extract linear instructions
    std::vector<std::vector<std::string>> parsed_instructions;
    addr_t current_pc = TEXT_BASE;

    for (const auto& stmt : statements) {
        for (const auto& lbl : stmt.labels) {
            symbol_table_[lbl] = current_pc;
        }
        if (!stmt.tokens.empty()) {
            parsed_instructions.push_back(stmt.tokens);
            current_pc += 4;
        }
    }
    for (const auto& lbl : pending_labels) {
        symbol_table_[lbl] = current_pc;
    }

    // Encoding helpers
    auto encodeReg = [](Opcode op, reg_t rd, reg_t rs1, reg_t rs2) -> word_t {
        return ((op & 0x1F) << 27) | (0U << 26) | ((rd & 0xF) << 22) | ((rs1 & 0xF) << 18) | ((rs2 & 0xF) << 14);
    };
    auto encodeImm = [](Opcode op, reg_t rd, reg_t rs1, uint16_t imm16) -> word_t {
        return ((op & 0x1F) << 27) | (1U << 26) | ((rd & 0xF) << 22) | ((rs1 & 0xF) << 18) | (imm16 & 0xFFFF);
    };

    // PASS 2: Machine words generation
    current_pc = TEXT_BASE;
    for (const auto& tokens : parsed_instructions) {
        std::string mnem = toUpper(tokens[0]);
        Opcode op = opMap.count(mnem) ? opMap.at(mnem) : OP_ADD;
        word_t machine_word = 0;
        reg_t r1 = 0, r2 = 0, r3 = 0;
        int32_t imm = 0;

        // Group 1: No arguments
        if (mnem == "NOP" || mnem == "RET" || mnem == "HALT") {
            machine_word = (static_cast<word_t>((mnem == "HALT") ? OP_NOP : op) << 27);
        } 
        // Group 2: Branches
        else if (mnem == "B" || mnem == "BEQ" || mnem == "BGT" || mnem == "CALL") {
            if (tokens.size() >= 2) {
                if (symbol_table_.count(tokens[1])) {
                    imm = (symbol_table_[tokens[1]] - current_pc) / 4;
                } else {
                    parseImmediate(tokens[1], imm);
                }
            }
            machine_word = ((op & 0x1F) << 27) | (imm & 0x07FFFFFF);
        } 
        // Group 3: Memory (Load/Store)
        else if (mnem == "LD" || mnem == "ST") {
            if (tokens.size() >= 3 && parseRegister(tokens[1], r1)) {
                if (tokens.size() >= 4) { // E.g., LD R1, 0x10(R2)
                    parseImmediate(tokens[2], imm);
                    parseRegister(tokens[3], r2);
                } else {                  // E.g., LD R1, R2
                    parseRegister(tokens[2], r2);
                }
                machine_word = encodeImm(op, r1, r2, imm);
            }
        } 
        // Group 4: Unary Register Conversions (I2D, D2I)
        else if (mnem == "I2D" || mnem == "D2I") {
            if (tokens.size() >= 3 && parseRegister(tokens[1], r1) && parseRegister(tokens[2], r2)) {
                machine_word = ((op & 0x1F) << 27) | (0U << 26) | ((r1 & 0xF) << 22) | ((r2 & 0xF) << 18);
            }
        }
        // Group 5: 2 Arguments (CMP, MOV, NOT)
        else if (mnem == "CMP" || mnem == "MOV" || mnem == "NOT") {
            reg_t rd = (mnem == "CMP") ? 0 : r1;
            if (tokens.size() >= 3 && parseRegister(tokens[1], r1)) {
                rd = (mnem == "CMP") ? 0 : r1;
                if (parseRegister(tokens[2], r2)) machine_word = encodeReg(op, rd, (mnem=="CMP")?r1:0, r2);
                else { parseImmediate(tokens[2], imm); machine_word = encodeImm(op, rd, (mnem=="CMP")?r1:0, imm); }
            }
        } 
        // Group 6: 3 Arguments (ALU Operations: ADD, SUB, MUL, FDIV, etc.)
        else {
            if (tokens.size() >= 4 && parseRegister(tokens[1], r1) && parseRegister(tokens[2], r2)) {
                if (parseRegister(tokens[3], r3)){
                    machine_word = encodeReg(op, r1, r2, r3);
                }
                else { 
                    parseImmediate(tokens[3], imm); 
                    machine_word = encodeImm(op, r1, r2, imm); 
                }
            }
        }
        result.machine_words.push_back(machine_word);
        current_pc += 4;
    }
    result.symbols = symbol_table_;
    return result;
}

} // namespace simplerisc