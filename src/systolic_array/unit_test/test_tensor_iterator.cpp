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
  auto refIter = iter.clone();
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

TEST_CASE("Test tensor index iterator", "[tensor-index]") {
  SECTION("Tensor shape without paddings") {
    TensorShape shape({ 4, 8, 8, 16 });
    TensorIndexIterator iter(shape);
    SECTION("Single step advance") {
      ++iter;
      REQUIRE(int(iter) == 1);
      for (int i = 0; i < 1234; i++)
        ++iter;
      REQUIRE(int(iter) == 1235);
    }
    SECTION("Region-based advance") {
      SECTION("Advance a region that doesn't go out of dimension bounds") {
        doRegionTest(iter, { 1, 2, 3, 4 });
        doRegionTest(iter, { 1, 3, 3, 3 });
      }
      SECTION("Advance a region that goes out of one dimension bound.") {
        doRegionTest(iter, { 0, 1, 1, 18 });
      }
      SECTION("Advance a region that goes out of multiple dimension bounds.") {
        doRegionTest(iter, { 0, 9, 10, 18 });
      }
      SECTION("Advance a region that triggers carries greater than 1") {
        doRegionTest(iter, { 0, 17, 10, 100 });
      }
    }
  }

  SECTION("Test tensor shape with alignment of 8 on the last dimension, and "
          "halo regions of {1, 1} on the H and W dimensions") {
    // After paddings, the shape becomes {4, 8, 8, 16}.
    TensorShape shape({ 4, 6, 6, 12 }, 8);
    std::vector<std::pair<int, int>> halo{
      { 0, 0 }, { 1, 1 }, { 1, 1 }, { 0, 0 }
    };
    TensorIndexIterator iter(shape, halo);
    SECTION("Test halo regions") {
      // The initial position of the iterator is in the halo region of indices
      // {0, -1, -1, 0}.
      REQUIRE(iter.getIndices() == std::vector<int>{ 0, -1, -1, 0 });
      REQUIRE(iter.inHaloRegion() == true);
      // Advance to the first non-halo region.
      iter += { 0, 1, 1, 0 };
      REQUIRE(iter.getIndices() == std::vector<int>{ 0, 0, 0, 0 });
      REQUIRE(iter.inHaloRegion() == false);
    }
    SECTION("Region-based advance") {
      SECTION("Advance a region that goes out of one dimension bound") {
        doRegionTest(iter, { 0, 1, 1, 18 });
      }
      SECTION("Advance a region that goes out of multiple dimension bounds") {
        doRegionTest(iter, { 0, 9, 10, 18 });
      }
      SECTION("Advance a region that triggers carries greater than 1") {
        doRegionTest(iter, { 0, 17, 10, 100 });
      }
    }
  }
}

TEST_CASE("Test tensor region index iterator", "[region-index]") {
  SECTION("A region index iterator with same origins as the tensor it "
          "iterators over") {
    TensorShape shape({ 4, 6, 6, 12 });
    TensorRegionIndexIterator iter(shape, { 0, 0, 0, 0 }, { 3, 3, 3, 3 });
    SECTION("Advance a region that doesn't go beyond any region bounds") {
      doRegionTest(iter, { 1, 1, 1, 1 });
    }
    SECTION("Advance a region that goes beyond one region bound") {
      doRegionTest(iter, { 1, 1, 1, 4 });
    }
    SECTION("Advance a region that goes beyond multiple region bounds") {
      doRegionTest(iter, { 1, 16, 8, 4 });
    }
  }
  SECTION("A region index iterator with different origins from the tensor it "
          "iterators over") {
    TensorShape shape({ 4, 8, 8, 12 });
    TensorRegionIndexIterator iter(shape, { 1, 1, 2, 2 }, { 3, 3, 3, 3 });
    SECTION("Advance a region that doesn't go beyond any region bounds") {
      doRegionTest(iter, { 1, 1, 1, 1 });
    }
    SECTION("Advance a region that goes beyond one region bound") {
      doRegionTest(iter, { 1, 1, 1, 4 });
    }
    SECTION("Advance a region that goes beyond multiple region bounds") {
      doRegionTest(iter, { 1, 16, 8, 4 });
    }
    SECTION("Advance a region that triggers carries greater than 1") {
      doRegionTest(iter, { 0, 0, 18, 13 });
    }
  }
}
