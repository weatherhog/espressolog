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
# -UNDEBUG and the per-file #undef NDEBUG are belt and braces, NOT a fix for a
# live hole — a draft of this comment claimed the latter. NDEBUG was never
# defined by this Makefile, so every assertion has always been compiled in and
# nothing changed behaviour. What they defend against is a future
# `make CXXFLAGS=-O2 -DNDEBUG`, which would otherwise silently compile away
# every check (several of these suites do work inside the assert, so the
# suites would not merely weaken, they would stop testing) and report a green
# run. The per-file #undef survives a command-line CXXFLAGS override; this
# flag does not.
CXXFLAGS = -std=c++17 -Wall -UNDEBUG

test: detector_test display_test machine_context_test machine_io_test armed_test brew_guard_test upload_policy_test spool_pick_test
	./detector_test
	./display_test
	./machine_context_test
	./machine_io_test
	./armed_test
	./brew_guard_test
	./upload_policy_test
	./spool_pick_test

detector_test: detector_test.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ detector_test.cpp ../../src/detector.cpp

display_test: display_test.cpp ../../src/display.cpp ../../src/display.h
	$(CXX) $(CXXFLAGS) -o $@ display_test.cpp ../../src/display.cpp

machine_context_test: machine_context_test.cpp ../../src/machine_context.h ../../src/display.cpp ../../src/display.h
	$(CXX) $(CXXFLAGS) -o $@ machine_context_test.cpp ../../src/display.cpp

machine_io_test: machine_io_test.cpp ../../src/machine_io.cpp ../../src/machine_io.h
	$(CXX) $(CXXFLAGS) -o $@ machine_io_test.cpp ../../src/machine_io.cpp

armed_test: armed_test.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ armed_test.cpp ../../src/detector.cpp

# machine_io.h, not .cpp: BrewGuard uses only SwitchBank's enum constants, so
# the suite links without the object — but renumbering the Line enum MUST
# rebuild this, and only the header dependency makes that happen.
brew_guard_test: brew_guard_test.cpp ../../src/brew_guard.cpp ../../src/brew_guard.h ../../src/machine_io.h
	$(CXX) $(CXXFLAGS) -o $@ brew_guard_test.cpp ../../src/brew_guard.cpp

upload_policy_test: upload_policy_test.cpp ../../src/upload_policy.cpp ../../src/upload_policy.h
	$(CXX) $(CXXFLAGS) -o $@ upload_policy_test.cpp ../../src/upload_policy.cpp

spool_pick_test: spool_pick_test.cpp ../../src/spool_pick.cpp ../../src/spool_pick.h
	$(CXX) $(CXXFLAGS) -o $@ spool_pick_test.cpp ../../src/spool_pick.cpp

replay: replay.cpp ../../src/detector.cpp ../../src/detector.h
	$(CXX) $(CXXFLAGS) -o $@ replay.cpp ../../src/detector.cpp

clean:
	rm -f detector_test display_test machine_context_test machine_io_test armed_test \
	      brew_guard_test upload_policy_test spool_pick_test replay
.PHONY: test clean
