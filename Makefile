# Graphicx - draw pictures, animations and video in a text terminal.
#
#   make            library (build/libgraphicx.a) + tools + examples
#   make lib        only the library
#   make test       build and run the unit tests
#   make clean
#
# Outputs: build/libgraphicx.a, bin/gfx-convert, bin/gfx-play, bin/example-*
# To use the library elsewhere: add -I<graphicx>/include and link
# build/libgraphicx.a (or compile the files in src/ into your project).

CXX      ?= c++
CXXFLAGS ?= -O2
CXXFLAGS += -std=c++11 -Wall -Wextra -Wpedantic -MMD -MP -Iinclude -isystem third_party

LIB      := build/libgraphicx.a
LIB_SRCS := src/graphicx.cpp src/image_io.cpp src/terminal.cpp src/color.cpp src/canvas.cpp src/ui.cpp src/widgets.cpp src/png.cpp
LIB_OBJS := $(LIB_SRCS:src/%.cpp=build/%.o)

TOOLS    := bin/gfx-convert bin/gfx-play
EXAMPLES := bin/example-hello bin/example-show-image bin/example-animation bin/example-canvas bin/example-dashboard
TESTS    := bin/test-graphicx bin/test-ui

.PHONY: all lib tools examples test clean

all: lib tools examples

lib: $(LIB)

tools: $(TOOLS)

examples: $(EXAMPLES)

$(LIB): $(LIB_OBJS)
	@mkdir -p $(dir $@)
	ar rcs $@ $^

build/%.o: src/%.cpp
	@mkdir -p $(dir $@)
	$(CXX) $(CXXFLAGS) -c $< -o $@

bin/gfx-%: tools/gfx-%.cpp $(LIB)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

bin/example-%: examples/%.cpp $(LIB)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

bin/test-graphicx: tests/test_graphicx.cpp $(LIB)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

bin/test-ui: tests/test_ui.cpp $(LIB)
	@mkdir -p bin
	$(CXX) $(CXXFLAGS) $< $(LIB) -o $@

test: $(TESTS)
	./bin/test-graphicx
	./bin/test-ui

clean:
	rm -rf build bin

-include $(LIB_OBJS:.o=.d)
