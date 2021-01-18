.PHONY: all gem5-accel clean

GEM5_SRCS = ../systolic_array_connection.cpp \
	    ../../aladdin/gem5/aladdin_sys_constants.cpp \
            ../../aladdin/gem5/aladdin_sys_connection.cpp

# For the MachSuite harness and support system.
CFLAGS = -O3 -Wall
ALL_SRCS = test.c

INCLUDES += -I../ -I../../ -I$(ALADDIN_HOME)/gem5 -I../../../ext/fp16/include
BMARK = test-gem5-accel

all: gem5-accel

gem5-accel: $(ALL_SRCS) $(GEM5_SRCS)
	@echo Building benchmark $(BMARK) for gem5 with HW acceleration.
	$(CC) $(CFLAGS) $^ $(INCLUDES) -o $(BMARK)

clean:
	rm -rf $(BMARK) outputs stdout stderr sram_*
