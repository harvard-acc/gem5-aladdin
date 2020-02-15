#ifndef __SYSTOLIC_ARRAY_UTILS_H__
#define __SYSTOLIC_ARRAY_UTILS_H__

#include "datatypes.h"
#include "fp16/include/fp16.h"

namespace systolic {

float16 fp16(float fp32_data);
float fp32(float16 fp16_data);

}  // namespace systolic

#endif
