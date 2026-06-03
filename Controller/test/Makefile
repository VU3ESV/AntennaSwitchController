# Host unit tests for the firmware decision logic (no hardware).
#   make run    build + run the tests
#   make clean  remove the binary
#
# Compiles the real firmware headers (../Config.h, ../Interlock.h, ../BandPlan.h)
# against the desktop shims in shims/. Angle-bracket includes (<Arduino.h>, …)
# resolve to shims/; quote includes ("Config.h", …) resolve to the firmware dir.
CXX      ?= c++
CXXFLAGS := -std=c++17 -Ishims -I.. -Wall -Wextra -Wno-unused-parameter

tests: tests.cpp ../Config.h ../Interlock.h ../BandPlan.h shims/*.h
	$(CXX) $(CXXFLAGS) -o tests tests.cpp

.PHONY: run clean
run: tests
	./tests

clean:
	rm -f tests
