.PHONY: all test clean

SYSTOLIC_HOME = $(ALADDIN_HOME)/../systolic_array

COMMON_OBJS = $(ALADDIN_HOME)/unit-test/catch_common.o \
              $(SYSTOLIC_HOME)/tensor.o
TEST_OBJS = test_tensor_iterator.o

TESTS = $(patsubst %.o,%,$(TEST_OBJS))

CFLAGS = -O3 -std=c++14 -I$(ALADDIN_HOME)/..
LFLAGS =

all: $(TESTS)

RUN_ALL_TESTS = $(patsubst %, run_test-%, $(TESTS))
run_test-%: %
	./$*
test: $(RUN_ALL_TESTS)

$(TESTS) : % : %.o $(COMMON_OBJS)
	$(CXX) -o $@ $*.o $(COMMON_OBJS) $(LFLAGS)

%.o : %.cpp
	$(CXX) -c $(CFLAGS) $< -o $*.o

clean:
	rm -f *.o
	rm -f $(TESTS) $(COMMON_OBJS)
	rm -rf $(TEST_REPORT_DIR)
