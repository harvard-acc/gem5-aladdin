#ifndef __SYSTOLIC_ARRAY_PE_H__
#define __SYSTOLIC_ARRAY_PE_H__

#include "debug/SystolicPE.hh"
#include "register.h"
#include "datatypes.h"
#include "utils.h"

namespace systolic {

class SystolicArray;

class MulAccUnit {
 public:
  MulAccUnit(Register<PixelData>::IO _input0,
             Register<PixelData>::IO _input1,
             Register<PixelData>::IO _input2,
             Register<PixelData>::IO _output,
             SystolicArray& _accel,
             const std::string& name)
      : input0(_input0), input1(_input1), input2(_input2), output(_output),
        accel(_accel), maccName(name) {}

 protected:
  void checkEndOfWindow();

  template <typename ElemType>
  void doMulAcc() {
    ElemType input0Data = *(input0->getDataPtr<ElemType>());
    ElemType input1Data = *(input1->getDataPtr<ElemType>());
    ElemType input2Data = (input2->isWindowEnd() || input2->size() == 0)
                              ? 0
                              : *(input2->getDataPtr<ElemType>());
    output->resize(input0->size());
    ElemType* outputData = output->getDataPtr<ElemType>();
    *outputData = input0Data * input1Data + input2Data;
    std::vector<int>& inputIndices = input0->indices;
    std::vector<int>& weightIndices = input1->indices;
    DPRINTF(SystolicPE,
            "IReg (%d, %d, %d, %d): %f, WReg (%d, %d, %d, %d): %f, OReg: %f.\n",
            inputIndices[0], inputIndices[1], inputIndices[2], inputIndices[3],
            (float)input0Data, weightIndices[0], weightIndices[1],
            weightIndices[2], weightIndices[3], (float)input1Data,
            (float)input2Data);
  }

 public:
  void evaluate();
  const std::string& name() const { return maccName; }

 protected:
  const std::string maccName;
  Register<PixelData>::IO input0;
  Register<PixelData>::IO input1;
  Register<PixelData>::IO input2;
  Register<PixelData>::IO output;
  SystolicArray& accel;
};

class ProcElem {
 public:
  ProcElem(const std::string& name, SystolicArray& accel)
      : peName(name), output0(), output1(), inputReg(), weightReg(),
        outputReg(), macc(inputReg.output(),
                          weightReg.output(),
                          outputReg.output(),
                          outputReg.input(),
                          accel,
                          name + ".macc") {}

  void evaluate() {
    // Perform the MACC operation.
    macc.evaluate();
    // Update the inputs to the registers of the next PE.
    if (output0.isConnected())
      *output0 = *inputReg.output();
    if (output1.isConnected())
      *output1 = *weightReg.output();
  }

  const std::string& name() const { return peName; }

 protected:
  const std::string peName;

 public:
  // This points to the input wire of the input register of the next PE down the
  // pipeline.
  Register<PixelData>::IO output0;
  // This points to the input wire of the weight register of the next PE down
  // the pipeline.
  Register<PixelData>::IO output1;
  // Make the registers/macc public so that others can access.
  Register<PixelData> inputReg;
  Register<PixelData> weightReg;
  Register<PixelData> outputReg;
  MulAccUnit macc;
};

}  // namespace systolic

#endif
