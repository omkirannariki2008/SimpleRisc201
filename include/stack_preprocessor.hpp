#ifndef SIMPLERISC_STACK_PREPROCESSOR_HPP
#define SIMPLERISC_STACK_PREPROCESSOR_HPP

#include <string>
#include <vector>
#include <cstdint>

namespace simplerisc {

struct PreprocessorReport {
    bool success{true};
    std::string expanded_source;
    std::string error_message;
    uint32_t macros_expanded{0};
    uint32_t original_lines{0};
    uint32_t generated_lines{0};
};

class StackPreprocessor {
public:
    StackPreprocessor() = default;

    // Process raw assembly source containing stack macros and return expanded source
    PreprocessorReport process(const std::string& source);

    // Process file directly
    PreprocessorReport processFile(const std::string& filepath);

private:
    std::vector<std::string> expandPush(const std::string& args);
    std::vector<std::string> expandPop(const std::string& args);
    std::vector<std::string> expandEnter(const std::string& args);
    std::vector<std::string> expandLeave(const std::string& args);

    static std::string trim(const std::string& s);
    static std::vector<std::string> splitTokens(const std::string& s, char delim = ',');
};

} // namespace simplerisc

#endif // SIMPLERISC_STACK_PREPROCESSOR_HPP
