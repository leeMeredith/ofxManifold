# ofxManifold — kernel test build.
#
# There is nothing to link. src/core includes no openFrameworks header, so the
# kernel builds and tests with a compiler and a vendored copy of glm. That is
# the whole point of the core/wrapper split: the mathematics can be proved
# green before any oF project exists.

CXX      ?= g++
CXXFLAGS ?= -std=c++17 -O2 -Wall -Wextra -Wpedantic -Ilibs

BUILD    := build

TRI_RUN  := $(BUILD)/run_vectors
MAN_RUN  := $(BUILD)/run_manifold
INT_RUN  := $(BUILD)/run_interpretation

TRI_VEC  := tests/vectors/triangle.vec
MAN_VEC  := tests/vectors/manifold.vec
INT_VEC  := tests/vectors/interpretation.vec

CORE     := src/core/ofxManifoldTypes.h \
            src/core/ofxManifoldTriangle.h \
            src/core/ofxManifold2D.h \
            src/core/ofxManifoldEvaluator.h

INTERP   := src/interpretation/ofxManifoldCurves.h \
            src/interpretation/ofxManifoldSpread.h \
            src/interpretation/ofxManifoldBlend.h

.PHONY: all test test-triangle test-manifold test-interpretation vectors clean

all: test

# Both suites must pass. They are run as separate targets rather than one
# binary so a failure names which layer broke: the solve, or the manifold.
test: test-triangle test-manifold test-interpretation
	@echo ""
	@echo "all suites green"

test-triangle: $(TRI_RUN) $(TRI_VEC)
	@./$(TRI_RUN) $(TRI_VEC)

test-manifold: $(MAN_RUN) $(MAN_VEC)
	@./$(MAN_RUN) $(MAN_VEC)

test-interpretation: $(INT_RUN) $(INT_VEC)
	@./$(INT_RUN) $(INT_VEC)

# Regenerate vectors from the Python references. Kept as a separate target so
# CI can assert the checked-in vectors match a fresh generation — a reference
# that drifts from its own output is worse than no reference.
vectors:
	@python3 tests/ref/reference.py
	@python3 tests/ref/reference_manifold.py
	@python3 tests/ref/reference_interpretation.py

$(TRI_RUN): tests/run_vectors.cpp $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_vectors.cpp

$(MAN_RUN): tests/run_manifold.cpp $(CORE)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_manifold.cpp

$(INT_RUN): tests/run_interpretation.cpp $(CORE) $(INTERP)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_interpretation.cpp

$(INT_VEC): tests/ref/reference_interpretation.py
	@python3 tests/ref/reference_interpretation.py

$(TRI_VEC): tests/ref/reference.py
	@python3 tests/ref/reference.py

$(MAN_VEC): tests/ref/reference_manifold.py
	@python3 tests/ref/reference_manifold.py

clean:
	@rm -rf $(BUILD)
