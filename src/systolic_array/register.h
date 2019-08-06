#ifndef __SYSTOLIC_ARRAY_REGISTER_H__
#define __SYSTOLIC_ARRAY_REGISTER_H__

#include "cpu/timebuf.hh"

namespace systolic {

template <typename ElemType>
class Register : public TimeBuffer<ElemType> {
 public:
  typedef TimeBuffer<ElemType> Buffer;
  Register()
      : Buffer(1, 0), inputWire(Buffer::getWire(0)),
        outputWire(Buffer::getWire(-1)) {}

  class IO {
   public:
    IO() : connected(false) {}
    IO(typename Buffer::wire _wireData)
        : connected(true), wireData(_wireData) {}

    bool isConnected() const { return connected; }
    ElemType& operator*() const { return *wireData; }
    ElemType* operator->() const { return wireData.operator->(); }

   protected:
    typename Buffer::wire wireData;
    bool connected;
  };

  /** An interface to just the input of the buffer */
  IO input() { return inputWire; }

  /** An interface to just the output of the buffer */
  IO output() { return outputWire; }

  void evaluate() { Buffer::advance(); }

 protected:
  IO inputWire;
  IO outputWire;
};

}  // namespace systolic

#endif
