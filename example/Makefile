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
DBG_PREFIX=--std=c++11 -O0 -g3 -DMCSL_ENABLE_STATS $(INCLUDE_PREFIX) -Wno-cpp -pthread
ASM_FILES=native.S

%.opt: %.cpp $(INCLUDE_FILES)
	$(CPP) $(OPT_PREFIX) $(ASM_FILES) -o $@ $< $(LINKER_PREFIX)

%.sta: %.cpp $(INCLUDE_FILES) $(ASM_FILES)
	$(CPP) $(STA_PREFIX) $(ASM_FILES) -o $@ $< $(LINKER_PREFIX)

%.dbg: %.cpp $(INCLUDE_FILES)
	$(CPP) $(DBG_PREFIX) $(ASM_FILES) -o $@ $< $(LINKER_PREFIX)

clean:
	rm -f *.o *.opt *.sta *.dbg 
