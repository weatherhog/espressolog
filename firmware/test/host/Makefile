# Host-side detector tools. The detector is pure logic (no Arduino deps),
# so it compiles and runs on the laptop:
#   make test    — scenario suite; every real-world failure becomes a case here
#   make replay  — reads a serial capture (t_ms,weight_mg[,state]) on stdin,
#                  replays it through the detector, prints completed records.
#                  This recovered a live shot on 2026-08-28.
#
# DisplayBus is pure logic too, and its test frames are real ones recorded
# off the machine (analysis/captures/), not synthetic bit patterns.
CXX ?= clang++
CXXFLAGS = -std=c++17 -Wall

test: detector_test display_test machine_context_test machine_io_test
	./detector_test
	./display_test
	./machine_context_test
	./machine_io_test

detector_test: detector_test.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ detector_test.cpp ../../src/detector.cpp

display_test: display_test.cpp ../../src/display.cpp ../../src/display.h
	$(CXX) $(CXXFLAGS) -o $@ display_test.cpp ../../src/display.cpp

machine_context_test: machine_context_test.cpp ../../src/machine_context.h ../../src/display.cpp ../../src/display.h
	$(CXX) $(CXXFLAGS) -o $@ machine_context_test.cpp ../../src/display.cpp

machine_io_test: machine_io_test.cpp ../../src/machine_io.cpp ../../src/machine_io.h
	$(CXX) $(CXXFLAGS) -o $@ machine_io_test.cpp ../../src/machine_io.cpp

replay: replay.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ replay.cpp ../../src/detector.cpp

clean:
	rm -f detector_test display_test machine_context_test machine_io_test replay
.PHONY: test clean
