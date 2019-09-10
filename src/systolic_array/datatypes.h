#ifndef __SYSTOLIC_ARRAY_DATATYPES_H__
#define __SYSTOLIC_ARRAY_DATATYPES_H__

#include <cstdint>
#include <cstring>
#include <cassert>
#include <vector>

namespace systolic {

enum DataType {
  UnknownDataType,
  Int32,
  Int64,
  Float16,
  Float32,
  Float64
};

using float16 = uint16_t;

// This is the pixel data that flows through the PEs. Other than the actual
// pixel data, a few other things are alos added, i.e., the original indices
// of the pixel in the tensor, whether the pixel is a bubble, and whether the
// pixel is the end of a convolution window (so that the commit unit will know
// when to collect an output pixel.)
class PixelData {
 public:
  PixelData() : bubble(true), windowEnd(false), pixel(0) {}
  PixelData(const PixelData& other)
      : indices(other.indices), bubble(other.bubble),
        windowEnd(other.windowEnd), pixel(other.pixel) {}

  void operator=(const PixelData& other) {
    pixel = other.pixel;
    indices = other.indices;
    bubble = other.bubble;
    windowEnd = other.windowEnd;
  }

  template <typename T>
  T* getDataPtr() {
    return reinterpret_cast<T*>(pixel.data());
  }

  void clear() {
    memset(pixel.data(), 0, pixel.size());
    bubble = true;
    windowEnd = false;
  }

  int size() const { return pixel.size(); }

  void resize(int size) { pixel.resize(size, 0); }

  bool isBubble() const { return bubble; }

  bool isWindowEnd() const { return windowEnd; }

  std::vector<uint8_t> pixel;
  // TODO: Technically, we don't need to store the whole indices of the pixel,
  // because we only need to know whether this pixel is the last element of the
  // window, so that the PE knows when to mark the output pixel for collection.
  // We pass the whole indices more for debugging purpose.
  std::vector<int> indices;
  bool bubble;
  bool windowEnd;
};

}  // namespace systolic

#endif

