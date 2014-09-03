#include "opcode_func.h"

bool is_associative(unsigned microop)
{
  if (microop == LLVM_IR_Add )
    return true;
  return false;
}
bool is_memory_op(unsigned microop)
{
  if (microop == LLVM_IR_Load || microop == LLVM_IR_Store)
    return true;
  return false;
}
bool is_compute_op(unsigned microop)
{
  switch(microop)
  {
    case LLVM_IR_Add : case LLVM_IR_FAdd : case LLVM_IR_Sub : case LLVM_IR_FSub : 
    case LLVM_IR_Mul : case LLVM_IR_FMul : case LLVM_IR_UDiv : case LLVM_IR_SDiv :
    case LLVM_IR_FDiv : case LLVM_IR_URem : case LLVM_IR_SRem : case LLVM_IR_FRem : 
    case LLVM_IR_Shl : case LLVM_IR_LShr : case LLVM_IR_AShr : case LLVM_IR_And :
    case LLVM_IR_Or: case LLVM_IR_Xor:
      return true;
    default:
      return false;
  }
}
bool is_store_op(unsigned microop)
{
  if (microop == LLVM_IR_Store)
    return true;
  return false;
}
bool is_load_op(unsigned microop)
{
  if (microop == LLVM_IR_Load)
    return true;
  return false;
}
bool is_bit_op(unsigned microop)
{
  switch (microop)
  {
    case LLVM_IR_Shl: case LLVM_IR_LShr: case LLVM_IR_AShr : case LLVM_IR_And : case LLVM_IR_Or: case LLVM_IR_Xor : 
      return true;
    default: 
      return false;
  }
}
bool is_branch_op (unsigned microop)
{
  switch (microop)
  {
    case LLVM_IR_Br : case LLVM_IR_Switch : case LLVM_IR_Call:
      return true;
    default:
      return false;
  }
}

bool is_call_op(unsigned microop)
{
  if (microop == LLVM_IR_Call)
    return true;
  return false;
}

bool is_control_op (unsigned microop)
{
  switch (microop)
  {
    case LLVM_IR_Br : case LLVM_IR_Switch : case LLVM_IR_PHI: case LLVM_IR_Call: 
      return true;
    default:
      return false;
  }
}

bool is_index_op (unsigned microop)
{
  if (microop == LLVM_IR_IndexAdd)
    return true;
  return false;
}

float node_latency (unsigned  microop)
{
  switch(microop)
  {
    case LLVM_IR_Add:  case LLVM_IR_Sub: 
      return ADD_LATENCY;
    case LLVM_IR_Mul: case LLVM_IR_UDiv: 
      return MUL_LATENCY;
    case LLVM_IR_Load : case LLVM_IR_Store: case LLVM_IR_Ret:
      return MEMOP_LATENCY;
    default:
      return 0;
  }
}
