CC       := clang-21
CXX      := clang++-21
LLVM_CONFIG := llvm-config-21
BUILD    := build

CFLAGS   := -std=c11 -Wall -Wextra -Werror -Iinclude
CXXFLAGS := -std=c++17 -Wall -Wextra -Wno-unused-parameter -Iinclude -Illvm \
            $(shell $(LLVM_CONFIG) --cxxflags)
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags)
LLVM_LIBS    := $(shell $(LLVM_CONFIG) --libs core analysis irreader passes support) \
                $(shell $(LLVM_CONFIG) --system-libs)

LIB_OBJS := $(BUILD)/rtm_model.o $(BUILD)/rll.o $(BUILD)/schedule.o

.PHONY: all check demo plugin cosim clean

all: $(BUILD)/test_rtm $(BUILD)/demo $(BUILD)/rtm-cost $(BUILD)/rtm_cosim $(BUILD)/rtm_cost.so

$(BUILD):
	mkdir -p $(BUILD)

$(BUILD)/%.o: src/%.c include/rtm.h | $(BUILD)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/test_rtm: tests/test_rtm.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(BUILD)/demo: src/demo.c $(LIB_OBJS) | $(BUILD)
	$(CC) $(CFLAGS) $< $(LIB_OBJS) -o $@

$(BUILD)/CostPass.o: llvm/CostPass.cpp llvm/CostPass.h include/rtm.h | $(BUILD)
	$(CXX) $(CXXFLAGS) -fPIC -c $< -o $@

$(BUILD)/rtm-cost: llvm/rtm_cost.cpp $(BUILD)/CostPass.o $(BUILD)/rtm_model.o | $(BUILD)
	$(CXX) $(CXXFLAGS) $< $(BUILD)/CostPass.o $(BUILD)/rtm_model.o \
	    $(LLVM_LDFLAGS) $(LLVM_LIBS) -o $@

$(BUILD)/rtm_cost.so: llvm/plugin.cpp $(BUILD)/CostPass.o $(BUILD)/rtm_model.o | $(BUILD)
	$(CXX) $(CXXFLAGS) -shared -fPIC $< $(BUILD)/CostPass.o $(BUILD)/rtm_model.o -o $@

$(BUILD)/ir_kernels.ll: kernels/ir_kernels.c | $(BUILD)
	$(CC) -O2 -fno-discard-value-names -fno-unroll-loops -fno-vectorize \
	    -fno-slp-vectorize -emit-llvm -S $< -o $@

$(BUILD)/rtm_cosim: rtl/rtm_ctrl.sv rtl/tb_rtm.sv src/dpi_bridge.c src/rtm_model.c include/rtm.h | $(BUILD)
	verilator --binary --timing -Wall --top-module tb_rtm -Mdir $(BUILD)/verilator \
	    -CFLAGS -I$(abspath include) \
	    rtl/rtm_ctrl.sv rtl/tb_rtm.sv src/dpi_bridge.c src/rtm_model.c \
	    -o $(abspath $(BUILD)/rtm_cosim)

check: $(BUILD)/test_rtm
	@$(BUILD)/test_rtm

demo: check $(BUILD)/demo $(BUILD)/rtm-cost $(BUILD)/ir_kernels.ll $(BUILD)/rtm_cosim $(BUILD)/rtm_cost.so
	@$(BUILD)/demo
	@echo
	@echo "5. LLVM shift-cost pass (ScalarEvolution on the same kernels)"
	@echo
	@$(BUILD)/rtm-cost $(BUILD)/ir_kernels.ll > $(BUILD)/rtm-cost.out
	@cat $(BUILD)/rtm-cost.out
	@grep -q "METRIC row_sum load A steady 1" $(BUILD)/rtm-cost.out
	@grep -q "METRIC col_sum load A steady 64" $(BUILD)/rtm-cost.out
	@grep -q "METRIC gather_sum load vals indirect 1" $(BUILD)/rtm-cost.out
	@grep -q "carry-shifts=63" $(BUILD)/rtm-cost.out
	@grep -q "carry-shifts=4032" $(BUILD)/rtm-cost.out
	@echo
	@echo "6. Same pass loaded into opt-21"
	@echo
	@opt-21 -load-pass-plugin=$(BUILD)/rtm_cost.so -passes=rtm-cost -disable-output \
	    $(BUILD)/ir_kernels.ll > $(BUILD)/opt-plugin.out
	@grep -q "METRIC col_sum load A steady 64" $(BUILD)/opt-plugin.out
	@grep -q "METRIC gather_sum load vals indirect 1" $(BUILD)/opt-plugin.out
	@echo "opt plugin: PASS"
	@echo
	@echo "7. DPI-C co-sim of rtm_ctrl against the C port model"
	@echo
	@$(BUILD)/rtm_cosim
	@test -s build/cosim_trace.csv
	@test -s build/la_trace.csv

plugin: $(BUILD)/rtm_cost.so

cosim: $(BUILD)/rtm_cosim

clean:
	rm -rf $(BUILD)
