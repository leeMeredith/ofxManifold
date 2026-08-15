# ofxManifold — kernel test build.
#
# There is nothing to link. src/core includes no openFrameworks header, so the
# kernel builds and tests with a compiler and a vendored copy of glm. That is
# the whole point of the core/wrapper split: the mathematics can be proved
# green before any oF project exists.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Ilibs

BUILD    := build
RUNNER   := $(BUILD)/run_vectors
VECTORS  := tests/vectors/triangle.vec

.PHONY: all test vectors clean

all: test

# Regenerate vectors from the Python reference, then run them. Kept as separate
# targets so CI can assert the checked-in vectors match a fresh generation —
# a reference that drifts from its own output is worse than no reference.
test: $(RUNNER) $(VECTORS)
	@./$(RUNNER) $(VECTORS)

vectors:
	@python3 tests/ref/reference.py

$(RUNNER): tests/run_vectors.cpp src/core/ofxManifoldTriangle.h src/core/ofxManifoldTypes.h
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_vectors.cpp

$(VECTORS): tests/ref/reference.py
	@python3 tests/ref/reference.py

clean:
	@rm -rf $(BUILD)
