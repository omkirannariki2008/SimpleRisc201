CXX ?= g++
CXXFLAGS ?= -std=c++14 -Wall -Wextra -O2 -I include

SRCS = src/alu.cpp \
       src/vertical_control_unit.cpp \
       src/horizontal_control_unit.cpp \
       src/cpu_pipeline.cpp \
       src/assembler.cpp \
       src/disassembler.cpp \
       src/stack_preprocessor.cpp \
       src/exception_framework.cpp \
       src/stack_visualizer.cpp \
       src/pipeline_visualizer.cpp \
       src/debugger_cli.cpp \
       src/main.cpp

BIN = simplerisc_sim.exe

all: $(BIN)

$(BIN): $(SRCS)
	$(CXX) $(CXXFLAGS) $(SRCS) -o $(BIN)

clean:
	rm -f $(BIN) NUL 2>/dev/null || del /Q $(BIN) 2>NUL || true

run: $(BIN)
	./$(BIN)

cli: $(BIN)
	./$(BIN) --cli

.PHONY: all clean run cli
