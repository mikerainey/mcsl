ifeq (, $(shell which jemalloc-config))
JEMALLOC =
else
JEMALLOCLD = $(shell jemalloc-config --libdir)
JEMALLOC = -L$(JEMALLOCLD) -ljemalloc
endif

INCLUDE_FILES=$(wildcard $(MCSL_INCLUDE_PATH)/*.hpp) $(wildcard $(CMDLINE_INCLUDE_PATH)/*.hpp)
INCLUDE_PREFIX=-I $(MCSL_INCLUDE_PATH) -DMCSL_TARGET_LINUX -I $(CMDLINE_INCLUDE_PATH)

LINKER_PREFIX=$(HWLOC_LIBRARY_PREFIX) $(JEMALLOC)

COMMON_PREFIX=--std=c++17 -pthread
DBG_PREFIX=$(COMMON_PREFIX) -O0 -g3 -DMCSL_ENABLE_STATS $(INCLUDE_PREFIX) -Wno-cpp
OPT_BASE_PREFIX=$(COMMON_PREFIX) -O2 -m64 -march=native --save-temps -DNDEBUG #  -fno-asynchronous-unwind-tables
OPT_PREFIX=$(OPT_BASE_PREFIX) $(INCLUDE_PREFIX)
STA_PREFIX=-DMCSL_ENABLE_STATS $(OPT_PREFIX)
LOG_PREFIX=-DMCSL_ENABLE_LOGGING $(STA_PREFIX)

native.o: native.S
	$(CC) -c native.S -o native.o

%.opt: %.cpp $(INCLUDE_FILES) native.o
	$(CPP) $(OPT_PREFIX) native.o -o $@ $< $(LINKER_PREFIX)

%.sta: %.cpp $(INCLUDE_FILES) native.o
	$(CPP) $(STA_PREFIX) native.o -o $@ $< $(LINKER_PREFIX)

%.log: %.cpp $(INCLUDE_FILES) native.o
	$(CPP) $(LOG_PREFIX) native.o -o $@ $< $(LINKER_PREFIX)

%.dbg: %.cpp $(INCLUDE_FILES) native.o
	$(CPP) $(DBG_PREFIX) native.o -o $@ $< $(LINKER_PREFIX)

clean:
	rm -f *.o *.opt *.sta *.dbg 
