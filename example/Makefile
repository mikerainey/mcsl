ifeq (, $(shell which jemalloc-config))
JEMALLOC =
else
JEMALLOCLD = $(shell jemalloc-config --libdir)
JEMALLOC = -L$(JEMALLOCLD) -ljemalloc
endif

INCLUDE_FILES=$(wildcard $(MCSL_INCLUDE_PATH)/*.hpp)
INCLUDE_PREFIX=-I $(MCSL_INCLUDE_PATH) -DMCSL_TARGET_LINUX

LINKER_PREFIX=$(HWLOC_LIBRARY_PREFIX) -lrt -ldl $(JEMALLOC)

OPT_BASE_PREFIX=-O2 -m64 -march=native --save-temps -fno-asynchronous-unwind-tables -DNDEBUG -pthread
OPT_PREFIX=--std=c++11 $(OPT_BASE_PREFIX) $(INCLUDE_PREFIX)
STA_PREFIX=-DMCSL_ENABLE_STATS $(OPT_PREFIX)
DBG_PREFIX=--std=c++11 -O0 -g3 $(INCLUDE_PREFIX) -Wno-cpp -pthread

%.opt: %.cpp $(INCLUDE_FILES)
	$(CPP) $(OPT_PREFIX) -o $@ $< $(LINKER_PREFIX)

%.sta: %.cpp $(INCLUDE_FILES) $(ASM_FILES)
	$(CPP) $(STA_PREFIX) -o $@ $< $(JEMALLOC) 

%.dbg: %.cpp $(INCLUDE_FILES)
	$(CPP) $(DBG_PREFIX) -o $@ $<

clean:
	rm -f *.o *.opt *.sta *.dbg 