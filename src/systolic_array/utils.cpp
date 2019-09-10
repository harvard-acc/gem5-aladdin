#include "utils.h"

namespace systolic {

float16 fp16(float fp32_data) { return fp16_ieee_from_fp32_value(fp32_data); }
float fp32(float16 fp16_data) { return fp16_ieee_to_fp32_value(fp16_data); }

}  // namespace systolic
