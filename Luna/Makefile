# This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall
INCLUDES = -I.
# Where the compiler finds the Luna definitions it loads itself (defs/ncg/ircmds.luna: define-ircmd and the headers)
DEFINES = -DLUNA_DEFS_DIR='"$(CURDIR)/defs"'

SOURCES = $(wildcard *.cpp) $(wildcard Core/*.cpp)
HEADERS = $(wildcard *.h) $(wildcard Core/*.h)
TARGET = ../build/debug/luwu-dsl-compiler

# Tests link the compiler without its main() and drive it in-process
TEST_SOURCES = $(filter-out Main.cpp,$(SOURCES)) $(wildcard tests/*.cpp)
TEST_TARGET = ../build/debug/luna-tests
ROOT = defs/luwu.luna
OUTPUT_DIR = ../CodeGen/src/generated
PUBLIC_OUTPUT_DIR = ../CodeGen/include/Luau/generated
# LoweringRules{A64,X64}.h in $(OUTPUT_DIR) are frozen while the micro-op pipeline replaces the rule compiler
OUTPUTS = $(PUBLIC_OUTPUT_DIR)/IrCmdInfo.h $(PUBLIC_OUTPUT_DIR)/IrCmd.h $(PUBLIC_OUTPUT_DIR)/IrCmdEffects.h

# Luna writes a depfile listing every file the root includes, so editing any of them reruns generation; the stamp
# records the last successful run, since outputs that didn't change keep their old timestamps
STAMP_DIR = ../build/luna
STAMP = $(STAMP_DIR)/generate.stamp
DEPFILE = $(STAMP_DIR)/generate.d

.PHONY: all generate test clean

all: generate

$(TARGET): $(SOURCES) $(HEADERS)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) $(SOURCES) -o $@

generate: $(STAMP)

# Missing outputs (e.g. deleted by hand) force a rerun even when the stamp is newer than every input
$(STAMP): $(TARGET) $(ROOT) $(if $(filter-out $(wildcard $(OUTPUTS)),$(OUTPUTS)),FORCE)
	@mkdir -p $(OUTPUT_DIR) $(PUBLIC_OUTPUT_DIR) $(STAMP_DIR)
	$(TARGET) $(ROOT) $(OUTPUT_DIR) --public-output $(PUBLIC_OUTPUT_DIR) -MF $(DEPFILE) -MT $(STAMP)
	@touch $@

FORCE:

-include $(DEPFILE)

$(TEST_TARGET): $(TEST_SOURCES) $(HEADERS) $(wildcard tests/*.h)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(DEFINES) $(INCLUDES) -I../extern $(TEST_SOURCES) -o $@

test: $(TEST_TARGET)
	$(TEST_TARGET)

clean:
	rm -f $(TARGET) $(TEST_TARGET) $(STAMP) $(DEPFILE)
