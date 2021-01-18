#include <iostream>
#include <vector>
#include <memory>

#include "aladdin/unit-test/catch.hpp"
#include "systolic_array/tensor.h"

using namespace systolic;

// Compute the reference results for region-based iterator operation. It's slow
// as it literally converts the region into single step movements, but it gives
// us the correct results.
std::vector<int> computeReferenceResults(const TensorIndexIterator& iter,
                                         const std::vector<int>& region) {
  std::unique_ptr<TensorIndexIterator> refIter(iter.clone());
  int numSingleSteps = 0;
  int stride = 1;
  for (int i = region.size() - 1; i >= 0; i--) {
    numSingleSteps += region[i] * stride;
    stride *= iter.getDimSize(i);
  }
  for (int i = 0; i < numSingleSteps; i++)
    ++(*refIter);
  return refIter->getIndices();
}

void doRegionTest(TensorIndexIterator& iter, const std::vector<int>& region) {
  std::vector<int> refResults(computeReferenceResults(iter, region));
  iter += region;
  REQUIRE(iter.getIndices() == refResults);
}

SCENARIO("Test tensor index iterator", "[tensor-index]") {
  GIVEN("Test tensor shape without paddings.") {
    TensorShape shape({ 4, 8, 8, 16 });
    TensorIndexIterator iter(shape);
    WHEN("Test single step advance.") {
      THEN("Advance the iterator by 1 step.") {
        ++iter;
        REQUIRE(int(iter) == 1);
      }
      THEN("Advance the iterator by a random number of single steps.") {
        for (int i = 0; i < 1234; i++)
          ++iter;
        REQUIRE(int(iter) == 1234);
      }
    }
    WHEN("Test regin-based advance.") {
      THEN("Advance the iterator by regions that don't go out of dimension "
           "bounds.") {
        doRegionTest(iter, { 1, 2, 3, 4 });
        doRegionTest(iter, { 1, 3, 3, 3 });
      }
      THEN("Advance the iterator by regions that go out of one dimension "
           "bound.") {
        doRegionTest(iter, { 0, 1, 1, 18 });
      }
      THEN("Advance the iterator by regions that go out of multiple dimension "
           "bounds.") {
        doRegionTest(iter, { 0, 9, 10, 18 });
      }
      THEN("Advance the iterator by regions that triggers carries greater than "
           "1.") {
        doRegionTest(iter, { 0, 17, 10, 100 });
      }
    }
  }

  GIVEN("Test tensor shape with alignment of 8 on the last dimension, and halo "
        "regions of {1, 1} on the H and W dimensions.") {
    // After paddings, the shape becomes {4, 8, 8, 16}.
    TensorShape shape({ 4, 6, 6, 12 }, 8);
    std::vector<std::pair<int, int>> halo{
      { 0, 0 }, { 1, 1 }, { 1, 1 }, { 0, 0 }
    };
    TensorIndexIterator iter(shape, halo);
    WHEN("Test halo region.") {
      THEN("The initial position of the iterator is in the halo region of "
           "indices {0, -1, -1, 0}.") {
        REQUIRE(iter.getIndices() == std::vector<int>{ 0, -1, -1, 0 });
        REQUIRE(iter.inHaloRegion() == true);
      }
      THEN("Advance to the first non-halo region.") {
        iter += {0, 1, 1, 0};
        REQUIRE(iter.getIndices() == std::vector<int>{ 0, 0, 0, 0 });
        REQUIRE(iter.inHaloRegion() == false);
      }
    }
    WHEN("Test regin-based advance.") {
      THEN("Advance the iterator by regions that go out of one dimension "
           "bound.") {
        doRegionTest(iter, { 0, 1, 1, 18 });
      }
      THEN("Advance the iterator by regions that go out of multiple dimension "
           "bounds.") {
        doRegionTest(iter, { 0, 9, 10, 18 });
      }
      THEN("Advance the iterator by regions that triggers carries greater than "
           "1.") {
        doRegionTest(iter, { 0, 17, 10, 100 });
      }
    }
  }
}

SCENARIO("Test tensor region index iterator", "[region-index]") {
  GIVEN("Test a region index iterator with same origins as the tensor it "
        "iterates over.") {
    TensorShape shape({ 4, 6, 6, 12 });
    TensorRegionIndexIterator iter(shape, { 0, 0, 0, 0 }, { 3, 3, 3, 3 });
    WHEN("Test region-based advance.") {
      THEN("Advance the region iterator by a region that doesn't go beyond any "
           "region bounds.") {
        doRegionTest(iter, { 1, 1, 1, 1 });
      }
      THEN("Advance the region iterator by a region that doesn't go beyond "
           "one region bound.") {
        doRegionTest(iter, { 1, 1, 1, 4 });
      }
      THEN("Advance the region iterator by a region that doesn't go beyond "
           "multiple region bounds.") {
        doRegionTest(iter, { 1, 16, 8, 4 });
      }
    }
  }
  GIVEN("Test a region index iterator with different origins from the tensor "
        "it iterates over.") {
    TensorShape shape({ 4, 8, 8, 12 });
    TensorRegionIndexIterator iter(shape, { 1, 1, 2, 2 }, { 3, 3, 3, 3 });
    WHEN("Test region-based advance.") {
      THEN("Advance the region iterator by a region that doesn't go beyond any "
           "region bounds.") {
        doRegionTest(iter, { 1, 1, 1, 1 });
      }
      THEN("Advance the region iterator by a region that doesn't go beyond "
           "one region bound.") {
        doRegionTest(iter, { 1, 1, 1, 4 });
      }
      THEN("Advance the region iterator by a region that doesn't go beyond "
           "multiple region bounds.") {
        doRegionTest(iter, { 1, 16, 8, 4 });
      }
      THEN("Advance the iterator by regions that triggers carries greater than "
           "1.") {
        doRegionTest(iter, { 0, 0, 18, 13 });
      }
    }
  }
}
