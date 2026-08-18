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
MAP_RUN  := $(BUILD)/run_mapping
SER_RUN  := $(BUILD)/run_serialize

TRI_VEC  := tests/vectors/triangle.vec
MAN_VEC  := tests/vectors/manifold.vec
INT_VEC  := tests/vectors/interpretation.vec
MAP_VEC  := tests/vectors/mapping.vec
SER_VEC  := tests/vectors/serialize.vec
# Sentinel for the generated fixture directory. Without this as a real
# prerequisite, a tree with the .vec file but no fixtures fails to run rather
# than regenerating -- and the runner's exit code for that is indistinguishable
# from a compile failure.
SER_FIX  := tests/fixtures/simple.json

CORE     := src/core/ofxManifoldTypes.h \
            src/core/ofxManifoldTriangle.h \
            src/core/ofxManifold2D.h \
            src/core/ofxManifoldEvaluator.h

INTERP   := src/interpretation/ofxManifoldCurves.h \
            src/interpretation/ofxManifoldSpread.h \
            src/interpretation/ofxManifoldBlend.h \
            src/interpretation/ofxManifoldInterpolate.h

MAPPING  := src/mapping/ofxManifoldMapping.h

IO       := src/io/ofxManifoldJSON.h \
            src/io/ofxManifoldSerialize.h

.PHONY: all test test-triangle test-manifold test-interpretation test-mapping test-serialize headers workflow vectors clean

all: test

# Both suites must pass. They are run as separate targets rather than one
# binary so a failure names which layer broke: the solve, or the manifold.
test: headers workflow test-triangle test-manifold test-interpretation test-mapping \
      test-serialize
	@echo ""
	@echo "all suites green"

# The CI workflow must stay loadable by GitHub. Cheap, and it catches the
# YAML 1.1 'on' -> true trap that silently invalidates the whole file.
workflow:
	@python3 tests/check_workflow.py

# Every header must compile ALONE, as the first thing in a translation unit.
# A header that relies on a transitive include compiles for whoever wrote it
# and fails for the next person, on a compiler whose standard library happens
# to nest things differently. That is not a portability nicety here: the whole
# point of src/core is that a stranger can drop it into their own project.
headers:
	@mkdir -p $(BUILD)
	@for h in $(CORE) $(INTERP) $(MAPPING) $(IO); do \
		printf '#include "%s"\nint main(){return 0;}\n' "$$h" > $(BUILD)/solo.cpp; \
		$(CXX) $(CXXFLAGS) -I. -fsyntax-only $(BUILD)/solo.cpp \
			|| { echo "  $$h does not compile standalone"; exit 1; }; \
	done
	@echo "all headers self-contained"

test-triangle: $(TRI_RUN) $(TRI_VEC)
	@./$(TRI_RUN) $(TRI_VEC)

test-manifold: $(MAN_RUN) $(MAN_VEC)
	@./$(MAN_RUN) $(MAN_VEC)

test-interpretation: $(INT_RUN) $(INT_VEC)
	@./$(INT_RUN) $(INT_VEC)

test-mapping: $(MAP_RUN) $(MAP_VEC)
	@./$(MAP_RUN) $(MAP_VEC)

test-serialize: $(SER_RUN) $(SER_VEC) $(SER_FIX)
	@./$(SER_RUN) $(SER_VEC) tests/fixtures

# Regenerate vectors from the Python references. Kept as a separate target so
# CI can assert the checked-in vectors match a fresh generation — a reference
# that drifts from its own output is worse than no reference.
vectors:
	@python3 tests/ref/reference.py
	@python3 tests/ref/reference_manifold.py
	@python3 tests/ref/reference_interpretation.py
	@python3 tests/ref/reference_mapping.py
	@python3 tests/ref/reference_serialize.py

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

$(MAP_RUN): tests/run_mapping.cpp $(CORE) $(INTERP) $(MAPPING)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_mapping.cpp

$(MAP_VEC): tests/ref/reference_mapping.py
	@python3 tests/ref/reference_mapping.py

$(SER_RUN): tests/run_serialize.cpp $(CORE) $(MAPPING) $(IO)
	@mkdir -p $(BUILD)
	$(CXX) $(CXXFLAGS) -o $@ tests/run_serialize.cpp

$(SER_VEC) $(SER_FIX): tests/ref/reference_serialize.py
	@python3 tests/ref/reference_serialize.py

$(TRI_VEC): tests/ref/reference.py
	@python3 tests/ref/reference.py

$(MAN_VEC): tests/ref/reference_manifold.py
	@python3 tests/ref/reference_manifold.py

clean:
	@rm -rf $(BUILD)
