#ifndef __SYSTOLIC_ARRAY_TENSOR_H__
#define __SYSTOLIC_ARRAY_TENSOR_H__

#include <vector>
#include <tuple>
#include <cassert>

namespace systolic {

template <typename T>
int product(std::vector<T> array) {
  int prod = 1;
  for (auto val : array)
    prod *= val;
  return prod;
}

template <typename T>
std::vector<T> sum(std::vector<T> array0, std::vector<T> array1) {
  assert(array0.size() == array1.size());
  std::vector<T> sum(array0.size());
  for (int i = 0; i < array0.size(); i++)
    sum[i] = array0[i] + array1[i];
  return sum;
}

class TensorShape {
 public:
  TensorShape() {}
  TensorShape(std::vector<int> _dims, int _alignment = 0)
      : dims_(_dims), padding_(dims_.size()), alignment(_alignment) {
    computePadding();
  }
  TensorShape(std::initializer_list<int> _dims, int _alignment = 0)
      : dims_(_dims), padding_(dims_.size()), alignment(_alignment) {
    computePadding();
  }
  TensorShape(const TensorShape& shape)
      : dims_(shape.dims_), padding_(shape.padding_),
        alignment(shape.alignment) {}

  const std::vector<int>& dims() const { return dims_; }
  const std::vector<int>& padding() const { return padding_; }
  int operator[](int index) const { return dims_[getIndex(index)]; }
  int& operator[](int index) { return dims_[getIndex(index)]; }
  int getStorageDim(int index) const {
    return dims_[getIndex(index)] + padding_[getIndex(index)];
  }
  bool operator==(const TensorShape& other) const {
    return (dims_ == other.dims_);
  }
  int ndims() const { return dims_.size(); }
  int size() const { return product(dims_); }
  int storageSize() const { return product(sum(dims_, padding_)); }
  int getAlignment() const { return alignment; }
  int getPadding(int index) const { return padding_[index]; }

 protected:
  int getIndex(int index) const {
    if (index >= 0)
      return index;
    return (dims_.size() + index);
  }

  int calcPadding(int value, unsigned alignment) {
    if (alignment == 0 || value % alignment == 0)
      return 0;
    return (alignment - (value % alignment));
  }

  void computePadding() {
    int ndims = dims_.size();
    padding_[ndims - 1] = calcPadding(dims_[ndims - 1], alignment);
    for (int i = 0; i < ndims - 1; i++)
      padding_[i] = 0;
  }

  std::vector<int> dims_;
  std::vector<int> padding_;
  int alignment;
};

// An iterator over a multidimensional tensor's indices, accounting for data
// alignment padding.
//
// The iterator tracks the current location as a coordinate and outputs the
// linearized index so that the data in a tensor can be accessed. While most
// commonly used to iterate through the contents of a tensor one by one, it can
// also provide random access to any location in the tensor.
//
// Example usage for simple iteration:
//   auto iter = TensorIndexIterator(tensor->getShape());
//   // OR: auto iter = tensor->startIndex();
//   float* data = tensor->data<float>();
//   while (!iter.end())
//      std::cout << data[iter] << ",";
//
// Example usage for random access (assume 4D tensor):
//   auto iter = TensorIndexIterator(tensor->getShape());
//   float* data = tensor->data<float>();
//   data[iter(1,2,3,4)] = 1.2;
//   data[iter(3,4,0,0)] = 3.4;
//
// The iterator skips over data alignment padding areas, if any exist.
class TensorIndexIterator {
 public:
  TensorIndexIterator() {}
  TensorIndexIterator(const TensorShape& shape, bool _atEnd = false)
      : dims(shape.dims()), effecDims(shape.dims()), padding(shape.padding()),
        halo(dims.size(), { 0, 0 }), atEnd(_atEnd),
        advanceOne(std::vector<int>(dims.size(), 0)) {
    computeEffectiveDims();
    advanceOne[dims.size() - 1] = 1;
    state.resize(dims.size());
    for (int i = 0; i < (int)state.size(); i++)
      state[i] = -halo[i].first;
  }
  TensorIndexIterator(const TensorShape& shape,
                      std::vector<std::pair<int, int>> _halo,
                      bool _atEnd = false)
      : dims(shape.dims()), effecDims(shape.dims()), padding(shape.padding()),
        halo(_halo), atEnd(_atEnd),
        advanceOne(std::vector<int>(dims.size(), 0)) {
    computeEffectiveDims();
    advanceOne[dims.size() - 1] = 1;
    state.resize(dims.size());
    for (int i = 0; i < (int)state.size(); i++)
      state[i] = -halo[i].first;
  }

  operator int() const { return getIndex(state); }

  bool end() const { return atEnd; }

  void operator=(const TensorIndexIterator& other) {
    state = other.state;
    dims = other.dims;
    effecDims = other.effecDims;
    padding = other.padding;
    halo = other.halo;
    atEnd = other.atEnd;
  }

  void operator++() { advanceRegion(advanceOne); }

  void operator+=(const std::vector<int>& region) {
    assert(region.size() == state.size());
    advanceRegion(region);
  }

  bool operator==(const TensorIndexIterator& other) const {
    return (state == other.state && dims == other.dims &&
            padding == other.padding && atEnd == other.atEnd);
  }

  bool operator!=(const TensorIndexIterator& other) const {
    return !(*this == other);
  }

  const std::vector<int>& getIndices() { return state; }

  bool inHaloRegion() const {
    for (int i = 0; i < state.size(); i++) {
      if (state[i] < 0 || state[i] >= dims[i])
        return true;
    }
    return false;
  }

  friend std::ostream& operator<<(std::ostream& os,
                                  const TensorIndexIterator& iter);

 protected:
  void computeEffectiveDims() {
    for (int i = 0; i < dims.size(); i++)
      effecDims[i] = dims[i] + padding[i] + halo[i].first + halo[i].second;
  }

  template <typename Container>
  int getIndex(Container indices) const {
    int linearIndex = 0, stride = 1;
    for (int i = (int)indices.size() - 1; i >= 0; i--) {
      linearIndex += indices[i] * stride;
      stride *= (dims.at(i) + padding.at(i));
    }
    return linearIndex;
  }

  virtual void advanceRegion(const std::vector<int>& region) {
    int carry = 0;
    for (int i = (int)state.size() - 1; i >= 0; i--) {
      int offset = carry + state[i] + region[i] + halo[i].first;
      std::tie(offset, carry) = computeOffsetAndCarry(offset, effecDims[i] - 1);
      state[i] = offset - halo[i].first;
    }
    atEnd = carry > 0;
  }

  // This computes the carry and the new offset when it goes beyond the
  // boundary.
  //
  // Args:
  //   offset: The current offset, which may be greater than boundary.
  //   bound: The boundary size of the this dimension.
  //   stride: The stride size of this dimension.
  //   nextStride: The stride size of the next dimension.
  //
  // Returns:
  //   A pair of integers, of which the first the new offset and the second is
  //   the carry size that will be added to the next dimension.
  virtual std::pair<int, int> computeOffsetAndCarry(int offset,
                                                    int bound,
                                                    int stride = 1,
                                                    int nextStride = 1) {
    if (offset > bound) {
      // The number of strides we are out of bound.
      int stridesOutBound = (offset - bound) / stride;
      // The number of strides this dimension can take.
      int stridesThisDim = bound / stride + 1;
      int carry = (stridesOutBound / stridesThisDim + 1) * nextStride;
      int newOffset = (stridesOutBound % stridesThisDim - 1) * stride;
      return {newOffset, carry};
    } else {
      return { offset, 0 };
    }
  }

  std::vector<int> state;
  std::vector<int> dims;
  std::vector<int> effecDims;
  std::vector<int> padding;
  std::vector<std::pair<int, int>> halo;
  bool atEnd;
  std::vector<int> advanceOne;
};

// A tensor index iterator that stays within a specified rectangular region.
//
// The rectangular region is specified using an origin coordinate and a region
// size. The iterator will output linear indices in the same space as the full
// tensor index iterator, but indices outside the region will be skipped.
//
// Example: consider a 3x3 tensor. The upper right 2x2 region's origin is at
// location (0,1). We can output just that block like so:
//
//    auto it = TensorRegionIndexIterator(tensor->getShape(), {0,1}, {2,2});
//    while (!it.end())
//       std::cout << (int)it << "\n";
//
//  This produces: 1, 2, 4, 5
class TensorRegionIndexIterator : public TensorIndexIterator {
 public:
  TensorRegionIndexIterator() {}
  TensorRegionIndexIterator(const TensorShape& shape,
                            const std::vector<int>& _origin,
                            const std::vector<int>& _regionSize)
      : TensorIndexIterator(shape, false), origin(_origin),
        regionSize(_regionSize), stride({ 1, 1, 1, 1 }) {
    state = origin;
  }
  TensorRegionIndexIterator(const TensorShape& shape,
                            std::vector<std::pair<int, int>> halo,
                            const std::vector<int>& _origin,
                            const std::vector<int>& _regionSize,
                            const std::vector<int>& _stride)
      : TensorIndexIterator(shape, halo, false), origin(_origin),
        regionSize(_regionSize), stride(_stride) {
    state = origin;
  }

  void operator=(const TensorRegionIndexIterator& other) {
    TensorIndexIterator::operator=(other);
    origin = other.origin;
    regionSize = other.regionSize;
    stride = other.stride;
  }

  // Advance the region to new origin indices. The advancing number of strides
  // is specified via advanceStrides.
  void advanceOriginByStride(const std::vector<int>& advanceStrides) {
    std::vector<int> advanceRegionSize(advanceStrides.size());
    for (int i = 0; i < advanceRegionSize.size(); i++)
      advanceRegionSize[i] = advanceStrides[i] * stride[i];
    advanceOrigin(advanceRegionSize);
  }

  // Set the region to new origin indices. The indices of the new place is
  // specified via _orign.
  void setOrigin(const std::vector<int>& _origin) {
    origin = _origin;
    advanceOrigin({ 0, 0, 0, 0 });
  }

 protected:
  // Advance the tensor region index with the specified region size.
  virtual void advanceRegion(const std::vector<int>& advanceRegionSize) {
    int carry = 0;
    for (int i = (int)state.size() - 1; i >= 0; i--) {
      // Offset relative to the origin of the region.
      int offset = carry + state[i] + advanceRegionSize[i] - origin[i];
      std::tie(offset, carry) =
          computeOffsetAndCarry(offset, regionSize[i] - 1);
      state[i] = offset + origin[i];
    }
    atEnd = carry > 0;
  }

  void advanceOrigin(const std::vector<int>& advanceRegionSize) {
    int carry = 0;
    for (int i = (int)state.size() - 1; i >= 0; i--) {
      // Offset relative to the origin of the whole tensor.
      int offset = carry + origin[i] + advanceRegionSize[i] + halo[i].first;
      // Compute the bound size.
      int strideThisDim = (effecDims[i] - regionSize[i]) / stride[i] + 1;
      int bound = (strideThisDim - 1) * stride[i];
      int nextStride = i > 0 ? stride[i - 1] : 1;
      std::tie(offset, carry) =
          computeOffsetAndCarry(offset, bound, stride[i], nextStride);
      origin[i] = offset - halo[i].first;
    }
    state = origin;
    atEnd = carry > 0;
  }

  std::vector<int> origin;
  std::vector<int> regionSize;
  std::vector<int> stride;
};

}  // namespace systolic

#endif
