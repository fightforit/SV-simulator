SLANG_DIR ?= /path/to/slang
GEN_DIR ?= gen
TOP ?= adder_tb
FILELIST ?= tests/file.f

CXX ?= g++
CXXFLAGS ?= -std=c++20 -Iinclude -I$(SLANG_DIR)/include -I$(SLANG_DIR)/build/source -I$(SLANG_DIR)/external
LDFLAGS ?= -L$(SLANG_DIR)/build/lib -lsvlang -lfmt -lmimalloc -pthread -ldl

SIM_SRCS = src/main.cpp src/frontend.cpp src/simulator.cpp src/codegen.cpp src/runtime.cpp
SIM_BIN = sim
GEN_SIM_SRCS = $(GEN_DIR)/sim_main.cpp src/runtime.cpp
GEN_BIN = $(GEN_DIR)/sim

ifeq ($(SLANG_DIR),/path/to/slang)
$(warning Set SLANG_DIR to your slang checkout, e.g., make SLANG_DIR=/path/to/slang)
endif

.PHONY: all sim gen gen_sim run clean

all: sim

sim: $(SIM_SRCS)
	$(CXX) $(CXXFLAGS) $(SIM_SRCS) $(LDFLAGS) -o $(SIM_BIN)

gen: sim
	./$(SIM_BIN) --top $(TOP) -file $(FILELIST) --cpp-out $(GEN_DIR) --no-sim

gen_sim: gen
	$(CXX) $(CXXFLAGS) $(GEN_SIM_SRCS) -Iinclude -o $(GEN_BIN)

run: gen_sim
	./$(GEN_BIN)

clean:
	rm -f $(SIM_BIN) $(GEN_BIN)
