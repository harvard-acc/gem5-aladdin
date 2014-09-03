#ifndef __DDDG_HH__
#define __DDDG_HH__

#include <string.h>
#include <stack>

#include "opcode_func.h"
#include "file_func.h"
#include "zlib.h"

//#include "datapath.hh"
/*#define HANDLE_INST(num, opc, clas) case num: return opc;*/
using namespace std;

struct edge_node_info{
  unsigned sink_node;
  int par_id;
};

//data structure used to tract dependency
typedef unordered_map<std::string, unsigned int> string_to_uint;
typedef unordered_map<long long int, unsigned int> uint_to_uint;
typedef unordered_multimap<unsigned int, edge_node_info> multi_uint_to_node_info;

class Datapath;

class DDDG
{
private: 
  Datapath *datapath;

public:
  DDDG(Datapath *_datapath);
  int num_edges();
  int num_nodes();
  int num_of_register_dependency();
  int num_of_memory_dependency();
  void output_method_call_graph(std::string bench);
  void output_dddg(std::string dddg_file, std::string edge_parid_file);
  void parse_instruction_line(std::string line);
  void parse_parameter(std::string line, int param_tag);
  void parse_result(std::string line);
  void parse_forward(std::string line);
  void parse_call_parameter(std::string line, int param_tag);
  int build_initial_dddg();

private:
  
  std::string curr_dynamic_function;
  int curr_microop;
  int prev_microop;
  std::string prev_bblock;
  std::string curr_bblock;
  
  std::string callee_function;
  std::string callee_dynamic_function;
  
  bool last_parameter;
  int num_of_parameters;
  //Used to track the instruction that initialize call function parameters
  int last_call_source;

  std::string curr_instid;
  std::vector<long long int> parameter_value_per_inst;
  std::vector<unsigned> parameter_size_per_inst;
  std::vector<std::string> parameter_label_per_inst;
  std::vector<std::string> method_call_graph;
  /*unordered_map<unsigned, bool> to_ignore_methodid;*/
  int num_of_instructions;
  int num_of_reg_dep;
  int num_of_mem_dep;

	//register dependency tracking table using hash_map(hash_map)
	//memory dependency tracking table
	//edge multimap
	multi_uint_to_node_info register_edge_table;
	multi_uint_to_node_info memory_edge_table;
	//keep track of currently executed methods
	stack<std::string> active_method;
	//manage methods
  /*c_string_to_uint method_appearance_table;*/
	string_to_uint function_counter;
  string_to_uint register_last_written;
	uint_to_uint address_last_written;
};

int build_initial_dddg(std::string bench, std::string trace_file_name);
#endif
