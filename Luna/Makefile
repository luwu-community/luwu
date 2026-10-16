# This file is part of the Luwu programming language and is licensed under MIT License; see LICENSE.txt for details
CXX ?= c++
CXXFLAGS ?= -std=c++17 -O2 -Wall
INCLUDES = -I. -ILowering

SOURCES = $(wildcard *.cpp) $(wildcard Lowering/*.cpp)
TARGET = ../build/debug/luwu-dsl-compiler
RULES = Lowering/rules.luna
OUTPUT_DIR = ../CodeGen/src/generated

.PHONY: all generate clean

all: generate

$(TARGET): $(SOURCES)
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) $(INCLUDES) $^ -o $@

generate: $(TARGET) $(RULES)
	@mkdir -p $(OUTPUT_DIR)
	$(TARGET) $(RULES) $(OUTPUT_DIR)

clean:
	rm -f $(TARGET)
