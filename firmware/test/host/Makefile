# Host-side detector tools. The detector is pure logic (no Arduino deps),
# so it compiles and runs on the laptop:
#   make test    — scenario suite; every real-world failure becomes a case here
#   make replay  — reads a serial capture (t_ms,weight_mg[,state]) on stdin,
#                  replays it through the detector, prints completed records.
#                  This recovered a live shot on 2026-08-28.
CXX ?= clang++
CXXFLAGS = -std=c++17 -Wall

test: detector_test
	./detector_test

detector_test: detector_test.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ detector_test.cpp ../../src/detector.cpp

replay: replay.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ replay.cpp ../../src/detector.cpp

clean:
	rm -f detector_test replay
.PHONY: test clean
