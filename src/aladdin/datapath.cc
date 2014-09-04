#include "aladdin/datapath.hh"
#include "debug/Datapath.hh"

Datapath::Datapath (const Params *p):
  MemObject(p), 
  benchName(p->benchName),
  traceFileName(p->traceFileName),
  configFileName(p->configFileName),
  cycleTime(p->cycleTime),
  dddg(this),
  scratchpad(this, 1),
  tickEvent(this)
{
  if(dddg.build_initial_dddg())
  {
    std::cerr << "-------------------------------" << std::endl;
    std::cerr << "       Aladdin Ends..          " << std::endl;
    std::cerr << "-------------------------------" << std::endl;
    exit(0);
  }
  
  parse_config();
  
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "    Initializing Datapath      " << std::endl;
  std::cerr << "-------------------------------" << std::endl;

  std::string bn(benchName);
  read_gzip_file_no_size(bn + "_microop.gz", microop);
  numTotalNodes = microop.size();
  std::vector<std::string> dynamic_methodid(numTotalNodes, "");
  initDynamicMethodID(dynamic_methodid);

  for (auto dynamic_func_it = dynamic_methodid.begin(), E = dynamic_methodid.end(); dynamic_func_it != E; dynamic_func_it++)
  {
    char func_id[256];
    int count;
    sscanf((*dynamic_func_it).c_str(), "%[^-]-%d\n", func_id, &count);
    if (functionNames.find(func_id) == functionNames.end())
      functionNames.insert(func_id);
  }

  setGlobalGraph();
  globalOptimizationPass();
  clearGlobalGraph();
  setGraphForStepping();
  cycle = 0;
  schedule(tickEvent, clockEdge(Cycles(1)));
}

Datapath::~Datapath()
{}

//optimizationFunctions
void Datapath::setGlobalGraph()
{
  graphName = benchName;
  std::cerr << "=============================================" << std::endl;
  std::cerr << "      Optimizing...            " << graphName << std::endl;
  std::cerr << "=============================================" << std::endl;
  finalIsolated.assign(numTotalNodes, 0);
}

void Datapath::clearGlobalGraph()
{
}

void Datapath::globalOptimizationPass()
{
  removeInductionDependence();
  removePhiNodes();
  initBaseAddress();
  completePartition();
  scratchpadPartition();
  loopFlatten();
  loopUnrolling();
  removeSharedLoads();
  storeBuffer();
  removeRepeatedStores();
  treeHeightReduction();
  loopPipelining();
}

void Datapath::memoryAmbiguation()
{
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "      Memory Ambiguation       " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  
  std::unordered_map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  
  std::unordered_multimap<std::string, std::string> pair_per_load;
  std::unordered_set<std::string> paired_store;
  std::unordered_map<std::string, bool> store_load_pair;

  std::vector<std::string> instid(numTotalNodes, "");
  std::vector<std::string> dynamic_methodid(numTotalNodes, "");
  std::vector<std::string> prev_basic_block(numTotalNodes, "");
  
  initInstID(instid);
  initDynamicMethodID(dynamic_methodid);
  initPrevBasicBlock(prev_basic_block);
  std::vector< Vertex > topo_nodes;
  boost::topological_sort(tmp_graph, std::back_inserter(topo_nodes));
  //nodes with no incoming edges to first
  for (auto vi = topo_nodes.rbegin(); vi != topo_nodes.rend(); ++vi)
  {
    unsigned node_id = vertex_to_name[*vi];
 
    int node_microop = microop.at(node_id);
    if (!is_store_op(node_microop))
      continue;
    //iterate its children to find a load op
    out_edge_iter out_edge_it, out_edge_end;
    for (tie(out_edge_it, out_edge_end) = out_edges(*vi, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
    {
      int child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
      int child_microop = microop.at(child_id);
      if (!is_load_op(child_microop))
        continue;
      std::string node_dynamic_methodid = dynamic_methodid.at(node_id);
      std::string load_dynamic_methodid = dynamic_methodid.at(child_id);
      if (node_dynamic_methodid.compare(load_dynamic_methodid) != 0)
        continue;
      
      std::string store_unique_id (node_dynamic_methodid + "-" + instid.at(node_id) + "-" + prev_basic_block.at(node_id));
      std::string load_unique_id (load_dynamic_methodid+ "-" + instid.at(child_id) + "-" + prev_basic_block.at(child_id));
      
      if (store_load_pair.find(store_unique_id + "-" + load_unique_id ) != store_load_pair.end())
        continue;
      //add to the pair
      store_load_pair[store_unique_id + "-" + load_unique_id] = 1;
      paired_store.insert(store_unique_id);
      auto load_range = pair_per_load.equal_range(load_unique_id);
      bool found_store = 0;
      for (auto store_it = load_range.first; store_it != load_range.second; store_it++)
      {
        if (store_unique_id.compare(store_it->second) == 0)
        {
          found_store = 1;
          break;
        }
      }
      if (!found_store)
      {
        pair_per_load.insert(make_pair(load_unique_id,store_unique_id));
      }
    }
  }
  if (store_load_pair.size() == 0)
    return;
  
  std::vector<newEdge> to_add_edges;
  std::unordered_map<std::string, unsigned> last_store;
  
  for (unsigned node_id = 0; node_id < numTotalNodes; node_id++)
  {
    int node_microop = microop.at(node_id);
    if (!is_memory_op(node_microop))
      continue;
    std::string unique_id (dynamic_methodid.at(node_id) + "-" + instid.at(node_id) + "-" + prev_basic_block.at(node_id));
    if (is_store_op(node_microop))
    {
      auto store_it = paired_store.find(unique_id);
      if (store_it == paired_store.end())
        continue;
      last_store[unique_id] = node_id;
    }
    else
    {
      assert(is_load_op(node_microop));
      auto load_range = pair_per_load.equal_range(unique_id);
      for (auto load_store_it = load_range.first; load_store_it != load_range.second; ++load_store_it)
      {
        assert(paired_store.find(load_store_it->second) != paired_store.end());
        auto prev_store_it = last_store.find(load_store_it->second);
        if (prev_store_it == last_store.end())
          continue;
        unsigned prev_store_id = prev_store_it->second;
        std::pair<Edge, bool> existed;
        existed = edge(name_to_vertex[prev_store_id], name_to_vertex[node_id], tmp_graph);
        if (existed.second == false)
        {
          to_add_edges.push_back({prev_store_id, node_id, -1});
          dynamicMemoryOps.insert(load_store_it->second + "-" + prev_basic_block.at(prev_store_id));
          dynamicMemoryOps.insert(load_store_it->first + "-" + prev_basic_block.at(node_id));
        }
      }
    }
  }
  writeGraphWithNewEdges(to_add_edges, num_of_edges);
}

void Datapath::removePhiNodes()
{
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "  Remove PHI and BitCast Nodes " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  
  unsigned num_of_edges = boost::num_edges(tmp_graph);

  std::vector<bool> to_remove_edges(num_of_edges, 0);
  std::vector<newEdge> to_add_edges;
  
  std::vector<int> edge_parid(num_of_edges, 0);
  initEdgeParID(edge_parid);
  
  vertex_iter vi, vi_end;
  int removed_phi = 0;
  for (tie(vi, vi_end) = vertices(tmp_graph); vi != vi_end; ++vi)
  {
    unsigned node_id = vertex_to_name[*vi];
    int node_microop = microop.at(node_id);
    if (node_microop != LLVM_IR_PHI && node_microop != LLVM_IR_BitCast)
      continue;
    //find its children

    std::vector< pair<unsigned, unsigned> > phi_child;

    out_edge_iter out_edge_it, out_edge_end;
    for (tie(out_edge_it, out_edge_end) = out_edges(*vi, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
    {
      unsigned edge_id = edge_to_name[*out_edge_it];
      unsigned child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
      to_remove_edges.at(edge_id) = 1;
      phi_child.push_back(make_pair(child_id, edge_id));
    }
    if (phi_child.size() == 0)
      continue;

    //find its parents
    in_edge_iter in_edge_it, in_edge_end;
    for (tie(in_edge_it, in_edge_end) = in_edges(*vi, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
    {
      unsigned parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
      int edge_id = edge_to_name[*in_edge_it];
      
      to_remove_edges.at(edge_id) = 1;
      for (auto child_it = phi_child.begin(), chil_E = phi_child.end(); child_it != chil_E; ++child_it)
      {
        unsigned child_id = child_it->first;
        unsigned child_edge_id  = child_it->second;
        to_add_edges.push_back({parent_id, child_id, edge_parid.at(child_edge_id)});
      }
    }
    removed_phi++;
  }
  int curr_num_of_edges = writeGraphWithIsolatedEdges(to_remove_edges);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();

}
void Datapath::initBaseAddress()
{
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "       Init Base Address       " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  
  std::unordered_map<std::string, unsigned> comp_part_config;
  readCompletePartitionConfig(comp_part_config);
  std::unordered_map<std::string, partitionEntry> part_config;
  readPartitionConfig(part_config);
  
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 

  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);

  std::unordered_map<unsigned, pair<std::string, long long int> > getElementPtr;
  initGetElementPtr(getElementPtr);
  
  vertex_iter vi, vi_end;
  for (tie(vi, vi_end) = vertices(tmp_graph); vi != vi_end; ++vi)
  {
    if (boost::degree(*vi, tmp_graph) == 0)
      continue;
    unsigned node_id = vertex_to_name[*vi];
    int node_microop = microop.at(node_id);
    if (!is_memory_op(node_microop))
      continue;
    bool flag_GEP = 0;
    bool no_gep_parent = 0;
    //iterate its parents, until it finds the root parent
    Vertex tmp_node;
    tmp_node = *vi;
    while (!no_gep_parent)
    {
      bool tmp_flag_GEP = 0;
      Vertex tmp_parent;

      in_edge_iter in_edge_it, in_edge_end;
      for (tie(in_edge_it, in_edge_end) = in_edges(tmp_node , tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
      {
        int parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
        int parent_microop = microop.at(parent_id);
        if (parent_microop == LLVM_IR_GetElementPtr || parent_microop == LLVM_IR_Load)
        {
          //remove address calculation directly
          baseAddress[node_id] = getElementPtr[parent_id];
          tmp_flag_GEP = 1;
          tmp_parent = source(*in_edge_it, tmp_graph);
          flag_GEP = 1;
          break;
        }
        else if (parent_microop == LLVM_IR_Alloca)
        {
          std::string part_name = getElementPtr[parent_id].first;
          baseAddress[node_id] = getElementPtr[parent_id];
          flag_GEP = 1;
          break;
        }
      }
      if (tmp_flag_GEP)
      {
        if (!flag_GEP)
          flag_GEP = 1;
        tmp_node = tmp_parent;
      }
      else
        no_gep_parent = 1;
    }
    if (!flag_GEP)
      baseAddress[node_id] = getElementPtr[node_id];
    
    std::string part_name = baseAddress[node_id].first;
    if (part_config.find(part_name) == part_config.end() &&
          comp_part_config.find(part_name) == comp_part_config.end() )
    {
      std::cerr << "Unknown partition : " << part_name << "@inst: " << node_id << std::endl;
      exit(0);
    }
  }
  writeBaseAddress();
}

void Datapath::loopFlatten()
{
  std::unordered_set<int> flatten_config;
  if (!readFlattenConfig(flatten_config))
    return;
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "         Loop Flatten          " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  std::vector<int> lineNum(numTotalNodes, -1);
  initLineNum(lineNum);
  
  unordered_set<unsigned> to_remove_nodes;

  for(unsigned node_id = 0; node_id < numTotalNodes; node_id++)
  {
    int node_linenum = lineNum.at(node_id);
    auto it = flatten_config.find(node_linenum);
    if (it == flatten_config.end())
      continue;
    if (is_compute_op(microop.at(node_id)))
      microop.at(node_id) = LLVM_IR_Move;
    else if (is_branch_op(microop.at(node_id)))
      to_remove_nodes.insert(node_id);
  }
  writeGraphWithIsolatedNodes(to_remove_nodes);
  cleanLeafNodes();
}
/*
 * Modify: graph, edgetype, edgelatency, baseAddress, microop
 * */
void Datapath::completePartition()
{
  std::unordered_map<std::string, unsigned> comp_part_config;
  if (!readCompletePartitionConfig(comp_part_config))
    return;
  
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "        Mem to Reg Conv        " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  
  for (auto it = comp_part_config.begin(); it != comp_part_config.end(); ++it)
  {
    std::string base_addr = it->first;
    unsigned size = it->second;

    scratchpad.setCompScratchpad(base_addr, size);
  }

}
void Datapath::cleanLeafNodes()
{
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  
  std::vector<int> edge_parid(num_of_edges, 0);
  initEdgeParID(edge_parid);
  
  /*track the number of children each node has*/
  std::vector<int> num_of_children(num_of_nodes, 0);
  unordered_set<unsigned> to_remove_nodes;
  
  std::vector< Vertex > topo_nodes;
  boost::topological_sort(tmp_graph, std::back_inserter(topo_nodes));
  //bottom nodes first
  for (auto vi = topo_nodes.begin(); vi != topo_nodes.end(); ++vi)
  {
    unsigned  node_id = vertex_to_name[*vi];
    if (boost::degree(*vi, tmp_graph) == 0)
      continue;
    int node_microop = microop.at(node_id);
    if (num_of_children.at(node_id) == boost::out_degree(*vi, tmp_graph) 
      && node_microop != LLVM_IR_SilentStore
      && node_microop != LLVM_IR_Store
      && node_microop != LLVM_IR_Ret 
      && node_microop != LLVM_IR_Br
      && node_microop != LLVM_IR_Switch
      && node_microop != LLVM_IR_Call)
    {
      to_remove_nodes.insert(node_id); 
      //iterate its parents
      in_edge_iter in_edge_it, in_edge_end;
      for (tie(in_edge_it, in_edge_end) = in_edges(*vi, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
      {
        int parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
        num_of_children.at(parent_id)++;
      }
    }
    else if (is_branch_op(node_microop))
    {
      //iterate its parents
      in_edge_iter in_edge_it, in_edge_end;
      for (tie(in_edge_it, in_edge_end) = in_edges(*vi, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
      {
        int edge_id = edge_to_name[*in_edge_it];
        if (edge_parid.at(edge_id) == CONTROL_EDGE)
        {
          int parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
          num_of_children.at(parent_id)++;
        }
      }
    }
  }
  edge_parid.clear();
  writeGraphWithIsolatedNodes(to_remove_nodes);
}

void Datapath::removeInductionDependence()
{
  //set graph
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "  Remove Induction Dependence  " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  
  std::vector<std::string> instid(numTotalNodes, "");
  initInstID(instid);

  std::vector< Vertex > topo_nodes;
  boost::topological_sort(tmp_graph, std::back_inserter(topo_nodes));
  //nodes with no incoming edges to first
  for (auto vi = topo_nodes.rbegin(); vi != topo_nodes.rend(); ++vi)
  {
    unsigned node_id = vertex_to_name[*vi];
    std::string node_instid = instid.at(node_id);
    
    if (node_instid.find("indvars") == std::string::npos)
      continue;
    if (microop.at(node_id) == LLVM_IR_Add )
        microop.at(node_id) = LLVM_IR_IndexAdd;
    //check its children, update the edge latency between them to 0
  }
}

/*
 * Modify: benchName_membase.gz
 * */
void Datapath::scratchpadPartition()
{
  //read the partition config file to get the address range
  // <base addr, <type, part_factor> > 
  std::unordered_map<std::string, partitionEntry> part_config;
  if (!readPartitionConfig(part_config))
    return;

  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "      ScratchPad Partition     " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  std::string bn(benchName);
  
  std::string partition_file;
  partition_file = bn + "_partition_config";

  std::unordered_map<unsigned, pair<long long int, unsigned> > address;
  initAddressAndSize(address);
  //set scratchpad
  for(auto it = part_config.begin(); it!= part_config.end(); ++it)
  {
    std::string base_addr = it->first;
    unsigned size = it->second.array_size; //num of words
    unsigned p_factor = it->second.part_factor;
    unsigned per_size = ceil(size / p_factor);
    
    for ( unsigned i = 0; i < p_factor ; i++)
    {
      ostringstream oss;
      oss << base_addr << "-" << i;
      scratchpad.setScratchpad(oss.str(), per_size);
    }
  }
  for(unsigned node_id = 0; node_id < numTotalNodes; node_id++)
  {
    int node_microop = microop.at(node_id);
    if (!is_memory_op(node_microop))
      continue;
    
    if (baseAddress.find(node_id) == baseAddress.end())
      continue;
    std::string base_label  = baseAddress[node_id].first;
    long long int base_addr = baseAddress[node_id].second;
    
    auto part_it = part_config.find(base_label);
    if (part_it != part_config.end())
    {
      std::string p_type = part_it->second.type;
      assert((!p_type.compare("block")) || (!p_type.compare("cyclic")));
      
      unsigned num_of_elements = part_it->second.array_size;
      unsigned p_factor        = part_it->second.part_factor;
      long long int abs_addr        = address[node_id].first;
      unsigned data_size       = address[node_id].second / 8; //in bytes
      unsigned rel_addr        = (abs_addr - base_addr ) / data_size; 
      if (!p_type.compare("block"))  //block partition
      {
        ostringstream oss;
        unsigned num_of_elements_in_2 = next_power_of_two(num_of_elements);
        oss << base_label << "-" << (int) (rel_addr / ceil (num_of_elements_in_2  / p_factor)) ;
        baseAddress[node_id].first = oss.str();
      }
      else // (!p_type.compare("cyclic")), cyclic partition
      {
        ostringstream oss;
        oss << base_label << "-" << (rel_addr) % p_factor;
        baseAddress[node_id].first = oss.str();
      }
    }
  }
}
//called in the end of the whole flow
void Datapath::dumpStats()
{
  writeMicroop(microop);
  writeFinalLevel();
  writeGlobalIsolated();
  writePerCycleActivity();
}

void Datapath::loopPipelining()
{
  if (!readPipeliningConfig())
  {
    std::cerr << "Loop Pipelining is not ON." << std::endl;
    return ;
  }
  
  std::unordered_map<int, int > unrolling_config;
  if (!readUnrollingConfig(unrolling_config))
  {
    std::cerr << "Loop Unrolling is not defined. " << std::endl;
    std::cerr << "Loop pipelining is only applied to unrolled loops." << std::endl;
    return ;
  }
  
  std::vector<int> loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  read_file(file_name, loop_bound);

  if (loop_bound.size() <= 2)
    return;
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "         Loop Pipelining        " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  
  std::vector<int> edge_parid(num_of_edges, 0);
  initEdgeParID(edge_parid);
  
  vertex_iter vi, vi_end;
  std::vector<bool> to_remove_edges(num_of_edges, 0);
  std::vector<newEdge> to_add_edges;
  
  //After loop unrolling, we define strick control dependences between basic block, 
  //where all the instructions in the following basic block depend on the prev branch instruction
  //During loop pipeling, to support loop pipelining, which allows the nex iteration 
  //starting without waiting until the prev iteration finish, we move the control dependences 
  //between last branch node in the prev basic block and instructions in the next basic block 
  //to first non isolated instruction in the prev basic block and instructions in the next basic block...
  std::map<unsigned, unsigned> first_non_isolated_node;
  auto loop_bound_it = loop_bound.begin();
  unsigned node_id = *loop_bound_it;
  loop_bound_it++;
  while ( (unsigned)node_id < num_of_nodes)
  {
    while (node_id < *loop_bound_it &&  (unsigned) node_id < num_of_nodes)
    {
      if(boost::degree(name_to_vertex[node_id], tmp_graph) == 0 
              || is_branch_op(microop.at(node_id)) )
      {
        node_id++;
        continue;
      }
      else
      {
        assert(is_branch_op(microop.at(*loop_bound_it)));
        assert(!is_branch_op(microop.at(node_id)));
        first_non_isolated_node[*loop_bound_it] = node_id;
        node_id = *loop_bound_it;
        break;
      }
    }
    loop_bound_it++;
    if (loop_bound_it == loop_bound.end() - 1 )
      break;
  }
  int prev_branch = -1;
  int prev_first = -1;
  for(auto first_it = first_non_isolated_node.begin(), E = first_non_isolated_node.end(); first_it != E; ++first_it)
  {
    unsigned br_node = first_it->first;
    //if br_node is a call instruction, skip
    if (is_call_op(microop.at(br_node)))
      continue;
    unsigned first_node = first_it->second;
    //all the nodes between first and branch now dependent on first
    if (prev_branch != -1)
    {
      out_edge_iter out_edge_it, out_edge_end;
      for (tie(out_edge_it, out_edge_end) = out_edges(name_to_vertex[prev_branch], tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
      {
        unsigned child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
        if (child_id <= first_node)
          continue;
        int edge_id = edge_to_name[*out_edge_it];
        if (edge_parid.at(edge_id) != CONTROL_EDGE) 
          continue;
        std::pair<Edge, bool> existed;
        existed = edge(name_to_vertex[first_node], name_to_vertex[child_id], tmp_graph);
        if (existed.second == false)
        {
          to_add_edges.push_back({first_node, child_id, 1});
        }
      }
    }
    //update first_node's parents, dependence become strict control dependence
    in_edge_iter in_edge_it, in_edge_end;
    for (tie(in_edge_it, in_edge_end) = in_edges(name_to_vertex[first_node], tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
    {
      unsigned parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
      if (is_branch_op(microop.at(parent_id)))
        continue;
      int edge_id = edge_to_name[*in_edge_it];
      to_remove_edges[edge_id] = 1;
      to_add_edges.push_back({parent_id, first_node, CONTROL_EDGE});
    }
    //adding dependence between prev_first and first_node
    if (prev_first != -1)
    {
      std::pair<Edge, bool> existed;
      existed = edge(name_to_vertex[prev_first], name_to_vertex[first_node], tmp_graph);
      if (existed.second == false)
      {
        to_add_edges.push_back({(unsigned)prev_first, first_node, CONTROL_EDGE});
      }
    }
    
    //remove control dependence between br node to its children
    out_edge_iter out_edge_it, out_edge_end;
    for (tie(out_edge_it, out_edge_end) = out_edges(name_to_vertex[br_node], tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
    {
      int edge_id = edge_to_name[*out_edge_it];
      if (edge_parid.at(edge_id) != CONTROL_EDGE) 
        continue;
      to_remove_edges[edge_id] = 1;
    }
    prev_branch = br_node;
    prev_first = first_node;
  }

  int curr_num_of_edges = writeGraphWithIsolatedEdges(to_remove_edges);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();
}
void Datapath::loopUnrolling()
{
  std::unordered_map<int, int > unrolling_config;
  readUnrollingConfig(unrolling_config);

  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "         Loop Unrolling        " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  std::unordered_map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  
  std::unordered_set<unsigned> to_remove_nodes;
  
  std::vector<int> lineNum(num_of_nodes, -1);
  initLineNum(lineNum);

  ofstream loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  loop_bound.open(file_name.c_str());
  bool first = 0;
  int iter_counts = 0;
  std::unordered_map<std::string, unsigned> inst_dynamic_counts;
  
  int prev_branch = -1;
  std::vector<unsigned> nodes_between;
  std::vector<newEdge> to_add_edges;

  for(unsigned node_id = 0; node_id < num_of_nodes; node_id++)
  {
    if (boost::degree(name_to_vertex[node_id], tmp_graph) == 0)
      continue;
    if (!first)
    {
      first = 1;
      loop_bound << node_id << std::endl;
    }
    if (prev_branch != -1)
      to_add_edges.push_back({(unsigned) prev_branch, node_id, CONTROL_EDGE});
    
    if (!is_branch_op(microop.at(node_id)))
    {
        nodes_between.push_back(node_id);
    }
    else
    {
      assert(is_branch_op(microop.at(node_id)));
      
      int node_linenum = lineNum.at(node_id);
      auto unroll_it = unrolling_config.find(node_linenum);
      //not unrolling branch
      if (unroll_it == unrolling_config.end())
      {
        for (auto prev_node_it = nodes_between.begin(), E = nodes_between.end();
                   prev_node_it != E; prev_node_it++)
        {
          std::pair<Edge, bool> existed;
          existed = edge(name_to_vertex[*prev_node_it], name_to_vertex[node_id], tmp_graph);
          if (existed.second == false)
            to_add_edges.push_back({*prev_node_it, node_id, CONTROL_EDGE});
        }
        
        nodes_between.clear();
        prev_branch = node_id;
      }
      else
      {
        int factor = unroll_it->second;
        int node_microop = microop.at(node_id);
        char unique_inst_id[256];
        sprintf(unique_inst_id, "%d-%d", node_microop, node_linenum);
        
        auto it = inst_dynamic_counts.find(unique_inst_id);
        if (it == inst_dynamic_counts.end())
        {
          inst_dynamic_counts[unique_inst_id] = 1;
          it = inst_dynamic_counts.find(unique_inst_id);
        }
        else
          it->second++;
        if (it->second % factor == 0)
        {
          loop_bound << node_id << std::endl;
          iter_counts++;
          for (auto prev_node_it = nodes_between.begin(), E = nodes_between.end();
                     prev_node_it != E; prev_node_it++)
          {
            std::pair<Edge, bool> existed;
            existed = edge(name_to_vertex[*prev_node_it], name_to_vertex[node_id], tmp_graph);
            if (existed.second == false)
              to_add_edges.push_back({*prev_node_it, node_id, CONTROL_EDGE});
          }
          
          nodes_between.clear();
          prev_branch = node_id;
        }
        else
          to_remove_nodes.insert(node_id);
      }
    }
  }
  loop_bound << num_of_nodes << std::endl;
  loop_bound.close();
  
  if (iter_counts == 0 && unrolling_config.size() != 0 )
  {
    std::cerr << "-------------------------------" << std::endl;
    std::cerr << "Loop Unrolling Factor is Larger than the Loop Trip Count." << std::endl;
    std::cerr << "Loop Unrolling is NOT applied. Please choose a smaller unrolling factor." << std::endl;
    std::cerr << "-------------------------------" << std::endl;
  }

  int curr_num_of_edges = writeGraphWithIsolatedNodes(to_remove_nodes);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();
}

void Datapath::removeSharedLoads()
{
  std::vector<int> loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  read_file(file_name, loop_bound);
  
  std::unordered_set<int> flatten_config;
  if (!readFlattenConfig(flatten_config)&& loop_bound.size() <= 2)
    return;
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "          Load Buffer          " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  std::unordered_map<unsigned, long long int> address;
  std::vector<int> edge_parid(num_of_edges, 0);

  initAddress(address);
  initEdgeParID(edge_parid);

  vertex_iter vi, vi_end;
  
  std::vector<bool> to_remove_edges(num_of_edges, 0);
  std::vector<newEdge> to_add_edges;

  int shared_loads = 0;
  auto loop_bound_it = loop_bound.begin();
  
  unsigned node_id = 0;
  while ( (unsigned)node_id < num_of_nodes)
  {
    std::unordered_map<unsigned, unsigned> address_loaded;
    while (node_id < *loop_bound_it &&  (unsigned) node_id < num_of_nodes)
    {
      if (boost::degree(name_to_vertex[node_id], tmp_graph) == 0)
      {
        node_id++;
        continue;
      }
      int node_microop = microop.at(node_id);
      long long int node_address = address[node_id];
      auto addr_it = address_loaded.find(node_address);
      if (is_store_op(node_microop) && addr_it != address_loaded.end())
        address_loaded.erase(addr_it);
      else if (is_load_op(node_microop))
      {
        if (addr_it == address_loaded.end())
          address_loaded[node_address] = node_id;
        else
        {
          shared_loads++;
          microop.at(node_id) = LLVM_IR_Move;
          unsigned prev_load = addr_it->second;
          //iterate throught its children
          Vertex load_node = name_to_vertex[node_id];
          
          out_edge_iter out_edge_it, out_edge_end;
          for (tie(out_edge_it, out_edge_end) = out_edges(load_node, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
          {
            unsigned child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
            int edge_id = edge_to_name[*out_edge_it];
            std::pair<Edge, bool> existed;
            existed = edge(name_to_vertex[prev_load], name_to_vertex[child_id], tmp_graph);
            if (existed.second == false)
              to_add_edges.push_back({prev_load, child_id, edge_parid.at(edge_id)});
            to_remove_edges[edge_id] = 1;
          }
          
          in_edge_iter in_edge_it, in_edge_end;
          for (tie(in_edge_it, in_edge_end) = in_edges(load_node, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
          {
            int edge_id = edge_to_name[*in_edge_it];
            to_remove_edges.at(edge_id) = 1;
          }
        }
      }
      node_id++;
    }
    loop_bound_it++;
    if (loop_bound_it == loop_bound.end() )
      break;
  }
  edge_parid.clear();
  int curr_num_of_edges = writeGraphWithIsolatedEdges(to_remove_edges);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();
}

void Datapath::storeBuffer()
{
  std::vector<int> loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  read_file(file_name, loop_bound);
  
  std::unordered_set<int> flatten_config;
  if (!readFlattenConfig(flatten_config)&& loop_bound.size() <= 2)
    return;

  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "          Store Buffer         " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  
  std::vector<int> edge_parid(num_of_edges, 0);
  initEdgeParID(edge_parid);
  
  std::vector<std::string> instid(numTotalNodes, "");
  std::vector<std::string> dynamic_methodid(numTotalNodes, "");
  std::vector<std::string> prev_basic_block(numTotalNodes, "");
  
  initInstID(instid);
  initDynamicMethodID(dynamic_methodid);
  initPrevBasicBlock(prev_basic_block);
  
  
  std::vector<bool> to_remove_edges(num_of_edges, 0);
  std::vector<newEdge> to_add_edges;

  int buffered_stores = 0;
  auto loop_bound_it = loop_bound.begin();
  
  unsigned node_id = 0;
  while (node_id < num_of_nodes)
  {
    while (node_id < *loop_bound_it && node_id < num_of_nodes)
    {
      if (boost::degree(name_to_vertex[node_id], tmp_graph) == 0)
      {
        node_id++;
        continue;
      }
      int node_microop = microop.at(node_id);
      if (is_store_op(node_microop))
      {
        Vertex node = name_to_vertex[node_id];
        out_edge_iter out_edge_it, out_edge_end;
        std::vector<unsigned> store_child;
        for (tie(out_edge_it, out_edge_end) = out_edges(node, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
        {
          int child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
          int child_microop = microop.at(child_id);
          if (is_load_op(child_microop))
          {
            std::string load_unique_id (dynamic_methodid.at(child_id) + "-" + instid.at(child_id) + "-" + prev_basic_block.at(child_id));
            if (dynamicMemoryOps.find(load_unique_id) != dynamicMemoryOps.end())
              continue;
            if (child_id >= (unsigned)*loop_bound_it )
              continue;
            else
              store_child.push_back(child_id);
          }
        }
        
        if (store_child.size() > 0)
        {
          buffered_stores++;
          
          in_edge_iter in_edge_it, in_edge_end;
          unsigned store_parent = num_of_nodes;
          for (tie(in_edge_it, in_edge_end) = in_edges(node, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
          {
            int edge_id = edge_to_name[*in_edge_it];
            int parent_id = vertex_to_name[source(*in_edge_it, tmp_graph)];
            int parid = edge_parid.at(edge_id);
            //parent node that generates value
            if (parid == 1)
            {
              store_parent = parent_id;
              break;
            }
          }
          
          if (store_parent != num_of_nodes)
          {
            for (unsigned i = 0; i < store_child.size(); ++i)
            {
              unsigned load_id = store_child.at(i);
              
              Vertex load_node = name_to_vertex[load_id];
              
              out_edge_iter out_edge_it, out_edge_end;
              for (tie(out_edge_it, out_edge_end) = out_edges(load_node, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
              {
                int edge_id = edge_to_name[*out_edge_it];
                unsigned child_id = vertex_to_name[target(*out_edge_it, tmp_graph)];
                to_remove_edges.at(edge_id) = 1;
                to_add_edges.push_back({store_parent, child_id, edge_parid.at(edge_id)});
              }
              
              for (tie(in_edge_it, in_edge_end) = in_edges(load_node, tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
              {
                int edge_id = edge_to_name[*in_edge_it];
                to_remove_edges.at(edge_id) = 1;
              }
            }
          }
        }
      }
      ++node_id; 
    }
    loop_bound_it++;
    if (loop_bound_it == loop_bound.end() )
      break;
  }
  edge_parid.clear();
  int curr_num_of_edges = writeGraphWithIsolatedEdges(to_remove_edges);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();
}

void Datapath::removeRepeatedStores()
{
  std::vector<int> loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  read_file(file_name, loop_bound);
  
  std::unordered_set<int> flatten_config;
  if (!readFlattenConfig(flatten_config)&& loop_bound.size() <= 2)
    return;
  
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "     Remove Repeated Store     " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;

  unsigned num_of_nodes = boost::num_vertices(tmp_graph);

  std::unordered_map<unsigned, long long int> address;
  initAddress(address);
  
  std::vector<std::string> instid(numTotalNodes, "");
  std::vector<std::string> dynamic_methodid(numTotalNodes, "");
  std::vector<std::string> prev_basic_block(numTotalNodes, "");
  
  initInstID(instid);
  initDynamicMethodID(dynamic_methodid);
  initPrevBasicBlock(prev_basic_block);
  
  vertex_iter vi, vi_end;

  int shared_stores = 0;
  int node_id = num_of_nodes - 1;
  auto loop_bound_it = loop_bound.end();
  loop_bound_it--;
  loop_bound_it--;
  while (node_id >=0 )
  {
    unordered_map<unsigned, int> address_store_map;
    while (node_id >= *loop_bound_it && node_id >= 0)
    {
      if (boost::degree(name_to_vertex[node_id], tmp_graph) == 0 )
      {
        --node_id;
        continue;
      }
      int node_microop = microop.at(node_id);
      if (is_store_op(node_microop))
      {
        long long int node_address = address[node_id];
        auto addr_it = address_store_map.find(node_address);
        if (addr_it == address_store_map.end())
          address_store_map[node_address] = node_id;
        else
        {
          //remove this store
          std::string store_unique_id (dynamic_methodid.at(node_id) + "-" + instid.at(node_id) + "-" + prev_basic_block.at(node_id));
          //dynamic stores, cannot disambiguated in the run time, cannot remove
          if (dynamicMemoryOps.find(store_unique_id) == dynamicMemoryOps.end())
          {
            Vertex node = name_to_vertex[node_id];
            //if it has children, ignore it
            if (boost::out_degree(node, tmp_graph)== 0)
            {
              microop.at(node_id) = LLVM_IR_SilentStore;
              shared_stores++;
            }
          }
        }
      }
      --node_id; 
    }
    if (loop_bound_it == loop_bound.begin())
      break;

    --loop_bound_it;
    
    if (loop_bound_it == loop_bound.begin())
      break;
  }
  cleanLeafNodes();
}

void Datapath::treeHeightReduction()
{
  std::vector<int> loop_bound;
  std::string file_name(graphName);
  file_name += "_loop_bound";
  read_file(file_name, loop_bound);
  std::unordered_set<int> flatten_config;
  if (!readFlattenConfig(flatten_config)&& loop_bound.size() <= 2)
    return;
  std::cerr << "-------------------------------" << std::endl;
  std::cerr << "     Tree Height Reduction     " << std::endl;
  std::cerr << "-------------------------------" << std::endl;
  //set graph
  Graph tmp_graph;
  readGraph(tmp_graph); 
  
  unsigned num_of_nodes = boost::num_vertices(tmp_graph);
  unsigned num_of_edges = boost::num_edges(tmp_graph);
   
  std::vector<int> edge_parid(num_of_edges, 0);
  initEdgeParID(edge_parid);
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, tmp_graph, Graph)
    name_to_vertex[get(boost::vertex_name, tmp_graph, v)] = v;
  
  std::vector<bool> updated(num_of_nodes, 0);
  std::vector<int> bound_region(num_of_nodes, 0);
  
  int region_id = 0;
  unsigned node_id = 0;
  auto b_it = loop_bound.begin();
  while (node_id < *b_it)
  {
    bound_region.at(node_id) = region_id;
    node_id++;
    if (node_id == *b_it)
    {
      region_id++;
      b_it++;
      if (b_it == loop_bound.end())
        break;
    }
  }
  
  std::vector<bool> to_remove_edges(num_of_edges, 0);
  std::vector<newEdge> to_add_edges;

  //nodes with no outgoing edges to first (bottom nodes first)
  for(int node_id = num_of_nodes -1; node_id >= 0; node_id--)
  {
    if(boost::degree(name_to_vertex[node_id], tmp_graph) == 0 || updated.at(node_id))
      continue;
    int node_microop = microop.at(node_id);
    if (!is_associative(node_microop))
      continue;
    updated.at(node_id) = 1;
    int node_region = bound_region.at(node_id); 
    std::list<unsigned> nodes;
    std::vector<int> tmp_remove_edges;
    std::vector<pair<int, bool> > leaves;
    
    std::vector<int> associative_chain;
    associative_chain.push_back(node_id);
    int chain_id = 0;
    while (chain_id < associative_chain.size())
    {
      int chain_node_id = associative_chain.at(chain_id);
      int chain_node_microop = microop.at(chain_node_id);
      if (is_associative(chain_node_microop))
      {
        updated.at(chain_node_id) = 1;
        in_edge_iter in_edge_it, in_edge_end;
        int num_of_chain_parents = 0;
        for (tie(in_edge_it, in_edge_end) = in_edges(name_to_vertex[chain_node_id] , tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
        {
          Vertex parent_node = source(*in_edge_it, tmp_graph);
          int parent_id = vertex_to_name[parent_node];
          int parent_microop = microop.at(parent_id);
          if (is_branch_op(parent_microop))
            continue;
          num_of_chain_parents++;
        }
        if (num_of_chain_parents == 2)
        {
          nodes.push_front(chain_node_id);
          for (tie(in_edge_it, in_edge_end) = in_edges(name_to_vertex[chain_node_id] , tmp_graph); in_edge_it != in_edge_end; ++in_edge_it)
          {
            Vertex parent_node = source(*in_edge_it, tmp_graph);
            int parent_id = vertex_to_name[parent_node];
            int parent_region = bound_region.at(parent_id);
            int parent_microop = microop.at(parent_id);
            if (is_branch_op(parent_microop))
              continue;
            int edge_id = edge_to_name[*in_edge_it];
            
            if (parent_region == node_region)
            {
              updated.at(parent_id) = 1;
              if (!is_associative(parent_microop))
              {
                tmp_remove_edges.push_back(edge_id);
                leaves.push_back(make_pair(parent_id, 0));
              }
              else
              {
                out_edge_iter out_edge_it, out_edge_end;
                int num_of_children = 0;
                for (tie(out_edge_it, out_edge_end) = out_edges(parent_node, tmp_graph); out_edge_it != out_edge_end; ++out_edge_it)
                {
                  int tmp_edge_id = edge_to_name[*out_edge_it];
                  if (edge_parid[tmp_edge_id] != CONTROL_EDGE)
                    num_of_children++;
                }
                if (num_of_children == 1)
                {
                  tmp_remove_edges.push_back(edge_id);
                  associative_chain.push_back(parent_id);
                }
                else
                {
                  tmp_remove_edges.push_back(edge_id);
                  leaves.push_back(make_pair(parent_id, 0));
                }
              }
            }
            else
            {
              leaves.push_back(make_pair(parent_id, 1));
              tmp_remove_edges.push_back(edge_id);
            }
          }
        }
        else
          leaves.push_back(make_pair(chain_node_id, 0));
      }
      else
        leaves.push_back(make_pair(chain_node_id, 0));
      chain_id++;
    }
    //build the tree
    if (nodes.size() < 3)
      continue;
    
    for(auto it = tmp_remove_edges.begin(), E = tmp_remove_edges.end(); it != E; it++)
      to_remove_edges.at(*it) = 1;

    std::map<unsigned, unsigned> rank_map;
    auto leaf_it = leaves.begin();
    
    while (leaf_it != leaves.end())
    {
      if (leaf_it->second == 0)
        rank_map[leaf_it->first] = 0;
      else
        rank_map[leaf_it->first] = num_of_nodes;
      ++leaf_it; 
    }
    //reconstruct the rest of the balanced tree
    auto node_it = nodes.begin();
    
    while (node_it != nodes.end())
    {
      unsigned node1, node2;
      if (rank_map.size() == 2)
      {
        node1 = rank_map.begin()->first;
        node2 = (++rank_map.begin())->first;
      }
      else
        findMinRankNodes(node1, node2, rank_map);
      assert((node1 != numTotalNodes) && (node2 != numTotalNodes));
      to_add_edges.push_back({node1, *node_it, 1});
      to_add_edges.push_back({node2, *node_it, 1});

      //place the new node in the map, remove the two old nodes
      rank_map[*node_it] = max(rank_map[node1], rank_map[node2]) + 1;
      rank_map.erase(node1);
      rank_map.erase(node2);
      ++node_it;
    }
  }
  int curr_num_of_edges = writeGraphWithIsolatedEdges(to_remove_edges);
  writeGraphWithNewEdges(to_add_edges, curr_num_of_edges);
  cleanLeafNodes();
}
void Datapath::findMinRankNodes(unsigned &node1, unsigned &node2, std::map<unsigned, unsigned> &rank_map)
{
  unsigned min_rank = numTotalNodes;
  for (auto it = rank_map.begin(); it != rank_map.end(); ++it)
  {
    int node_rank = it->second;
    if (node_rank < min_rank)
    {
      node1 = it->first;
      min_rank = node_rank;
    }
  }
  min_rank = numTotalNodes;
  for (auto it = rank_map.begin(); it != rank_map.end(); ++it)
  {
    int node_rank = it->second;
    if ((it->first != node1) && (node_rank < min_rank))
    {
      node2 = it->first;
      min_rank = node_rank;
    }
  }
}

int Datapath::writeGraphWithNewEdges(std::vector<newEdge> &to_add_edges, int curr_num_of_edges)
{
  std::string gn(graphName);
  std::string graph_file, edge_parid_file;
  graph_file = gn + "_graph";
  edge_parid_file = gn + "_edgeparid.gz";
  
  ifstream orig_graph;
  ofstream new_graph;
  gzFile new_edgeparid;
  
  orig_graph.open(graph_file.c_str());
  std::filebuf *pbuf = orig_graph.rdbuf();
  std::size_t size = pbuf->pubseekoff(0, orig_graph.end, orig_graph.in);
  pbuf->pubseekpos(0, orig_graph.in);
  char *buffer = new char[size];
  pbuf->sgetn (buffer, size);
  orig_graph.close();
  
  new_graph.open(graph_file.c_str());
  new_graph.write(buffer, size);
  delete[] buffer;

  long pos = new_graph.tellp();
  new_graph.seekp(pos-2);

  new_edgeparid = gzopen(edge_parid_file.c_str(), "a");
  
  int new_edge_id = curr_num_of_edges;
  
  for(auto it = to_add_edges.begin(); it != to_add_edges.end(); ++it)
  {
    new_graph << it->from << " -> " 
              << it->to
              << " [e_id = " << new_edge_id << "];" << std::endl;
    new_edge_id++;
    gzprintf(new_edgeparid, "%d\n", it->parid);
  }
  new_graph << "}" << std::endl;
  new_graph.close();
  gzclose(new_edgeparid);

  return new_edge_id;
}
int Datapath::writeGraphWithIsolatedNodes(std::unordered_set<unsigned> &to_remove_nodes)
{
  Graph tmp_graph;
  readGraph(tmp_graph);
  
  unsigned num_of_edges = num_edges(tmp_graph);
  
  std::vector<int> edge_parid(num_of_edges, 0);

  initEdgeParID(edge_parid);
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);

  ofstream new_graph;
  gzFile new_edgeparid;
  
  std::string gn(graphName);
  std::string graph_file, edge_parid_file;
  graph_file = gn + "_graph";
  edge_parid_file = gn + "_edgeparid.gz";
  
  new_graph.open(graph_file.c_str());
  new_edgeparid = gzopen(edge_parid_file.c_str(), "w");
  
  new_graph << "digraph DDDG {" << std::endl;

  for (unsigned node_id = 0; node_id < numTotalNodes; node_id++)
     new_graph << node_id << ";" << std::endl; 
  
  edge_iter ei, ei_end;
  int new_edge_id = 0;
  for (tie(ei, ei_end) = edges(tmp_graph); ei != ei_end; ++ei)
  {
    int edge_id = edge_to_name[*ei];
    int from = vertex_to_name[source(*ei, tmp_graph)];
    int to   = vertex_to_name[target(*ei, tmp_graph)];
    if (to_remove_nodes.find(from) != to_remove_nodes.end() 
     || to_remove_nodes.find(to) != to_remove_nodes.end())
      continue;

    new_graph << from << " -> " 
              << to
              << " [e_id = " << new_edge_id << "];" << std::endl;
    new_edge_id++;
    gzprintf(new_edgeparid, "%d\n", edge_parid.at(edge_id) );
  }

  new_graph << "}" << std::endl;
  new_graph.close();
  gzclose(new_edgeparid);
  
  return new_edge_id;
}
int Datapath::writeGraphWithIsolatedEdges(std::vector<bool> &to_remove_edges)
{
  Graph tmp_graph;
  readGraph(tmp_graph);
  
  unsigned num_of_edges = num_edges(tmp_graph);
  unsigned num_of_nodes = num_vertices(tmp_graph);

  std::vector<int> edge_parid(num_of_edges, 0);

  initEdgeParID(edge_parid);
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);

  ofstream new_graph;
  gzFile new_edgeparid;
  
  std::string gn(graphName);
  std::string graph_file, edge_parid_file;
  graph_file = gn + "_graph";
  edge_parid_file = gn + "_edgeparid.gz";
  
  new_graph.open(graph_file.c_str());
  new_edgeparid = gzopen(edge_parid_file.c_str(), "w");

  new_graph << "digraph DDDG {" << std::endl;

  for (unsigned node_id = 0; node_id < num_of_nodes; node_id++)
     new_graph << node_id << ";" << std::endl; 
  
  edge_iter ei, ei_end;
  int new_edge_id = 0;
  for (tie(ei, ei_end) = edges(tmp_graph); ei != ei_end; ++ei)
  {
    int edge_id = edge_to_name[*ei];
    if (to_remove_edges.at(edge_id))
      continue;
    new_graph << vertex_to_name[source(*ei, tmp_graph)] << " -> " 
              << vertex_to_name[target(*ei, tmp_graph)] 
              << " [e_id = " << new_edge_id << "];" << std::endl;
    new_edge_id++;
    gzprintf(new_edgeparid, "%d\n", edge_parid.at(edge_id) );
  }
  
  new_graph << "}" << std::endl;
  new_graph.close();
  gzclose(new_edgeparid);
  return new_edge_id;
}

void Datapath::readGraph(Graph &tmp_graph)
{
  std::string gn(graphName);
  std::string graph_file_name(gn + "_graph");
  
  boost::dynamic_properties dp;
  boost::property_map<Graph, boost::vertex_name_t>::type v_name = get(boost::vertex_name, tmp_graph);
  boost::property_map<Graph, boost::edge_name_t>::type e_name = get(boost::edge_name, tmp_graph);
  dp.property("n_id", v_name);
  dp.property("e_id", e_name);
  std::ifstream fin(graph_file_name.c_str());
  boost::read_graphviz(fin, tmp_graph, dp, "n_id");

}

//initFunctions
void Datapath::writePerCycleActivity()
{
  std::string bn(benchName);
  
  std::vector<std::string> dynamic_methodid(numTotalNodes, "");
  initDynamicMethodID(dynamic_methodid);
  
  std::unordered_map< std::string, std::vector<int> > mul_activity;
  std::unordered_map< std::string, std::vector<int> > add_activity;
  std::unordered_map< std::string, std::vector<int> > bit_activity;
  std::unordered_map< std::string, std::vector<int> > ld_activity;
  std::unordered_map< std::string, std::vector<int> > st_activity;
  
  std::vector<std::string> partition_names;
  std::vector<std::string> comp_partition_names;
  scratchpad.partitionNames(partition_names);
  scratchpad.compPartitionNames(comp_partition_names);

  float avg_power, avg_fu_power, avg_mem_power, total_area, fu_area, mem_area;
  mem_area = 0;
  fu_area = 0;
  for (auto it = partition_names.begin(); it != partition_names.end() ; ++it)
  {
    std::string p_name = *it;
    ld_activity.insert({p_name, make_vector(cycle)});
    st_activity.insert({p_name, make_vector(cycle)});
    mem_area += scratchpad.area(*it);
  }
  for (auto it = comp_partition_names.begin(); it != comp_partition_names.end() ; ++it)
  {
    std::string p_name = *it;
    ld_activity.insert({p_name, make_vector(cycle)});
    st_activity.insert({p_name, make_vector(cycle)});
    fu_area += scratchpad.area(*it);
  }
  for (auto it = functionNames.begin(); it != functionNames.end() ; ++it)
  {
    std::string p_name = *it;
    mul_activity.insert({p_name, make_vector(cycle)});
    add_activity.insert({p_name, make_vector(cycle)});
    bit_activity.insert({p_name, make_vector(cycle)});
  }
  for(unsigned node_id = 0; node_id < numTotalNodes; ++node_id)
  {
    if (finalIsolated.at(node_id))
      continue;
    int tmp_level = newLevel.at(node_id);
    int node_microop = microop.at(node_id);
    char func_id[256];
    int count;
    sscanf(dynamic_methodid.at(node_id).c_str(), "%[^-]-%d\n", func_id, &count);
    
    if (node_microop == LLVM_IR_Mul || node_microop == LLVM_IR_UDiv)
      mul_activity[func_id].at(tmp_level) +=1;
    else if  (node_microop == LLVM_IR_Add || node_microop == LLVM_IR_Sub)
      add_activity[func_id].at(tmp_level) +=1;
    else if (is_bit_op(node_microop))
      bit_activity[func_id].at(tmp_level) +=1;
    else if (is_load_op(node_microop))
    {
      std::string base_addr = baseAddress[node_id].first;
      ld_activity[base_addr].at(tmp_level) += 1;
    }
    else if (is_store_op(node_microop))
    {
      std::string base_addr = baseAddress[node_id].first;
      st_activity[base_addr].at(tmp_level) += 1;
    }
  }
  ofstream stats, power_stats;
  std::string tmp_name = bn + "_stats";
  stats.open(tmp_name.c_str());
  tmp_name += "_power";
  power_stats.open(tmp_name.c_str());

  stats << "cycles," << cycle << "," << numTotalNodes << std::endl; 
  power_stats << "cycles," << cycle << "," << numTotalNodes << std::endl; 
  stats << cycle << "," ;
  power_stats << cycle << "," ;
  
  int max_mul =  0;
  int max_add =  0;
  int max_bit =  0;
  int max_reg_read =  0;
  int max_reg_write =  0;
  for (unsigned level_id = 0; ((int) level_id) < cycle; ++level_id)
  {
    if (max_reg_read < regStats.at(level_id).reads )
      max_reg_read = regStats.at(level_id).reads ;
    if (max_reg_write < regStats.at(level_id).writes )
      max_reg_write = regStats.at(level_id).writes ;
  }
  int max_reg = max_reg_read + max_reg_write;

  for (auto it = functionNames.begin(); it != functionNames.end() ; ++it)
  {
    stats << *it << "-mul," << *it << "-add," << *it << "-bit,";
    power_stats << *it << "-mul," << *it << "-add," << *it << "-bit,";
    max_bit += *max_element(bit_activity[*it].begin(), bit_activity[*it].end());
    max_add += *max_element(add_activity[*it].begin(), add_activity[*it].end());
    max_mul += *max_element(mul_activity[*it].begin(), mul_activity[*it].end());
  }

  //ADD_int_power, MUL_int_power, REG_int_power
  float add_leakage_per_cycle = ADD_leak_power * max_add;
  float mul_leakage_per_cycle = MUL_leak_power * max_mul;
  float reg_leakage_per_cycle = REG_leak_power * 32 * max_reg;

  fu_area += ADD_area * max_add + MUL_area * max_mul + REG_area * 32 * max_reg;
  total_area = mem_area + fu_area;
  
  for (auto it = partition_names.begin(); it != partition_names.end() ; ++it)
  {
    stats << *it << "," ;
    power_stats << *it << "," ;
  }
  stats << "reg" << std::endl;
  power_stats << "reg" << std::endl;

  avg_power = 0;
  avg_fu_power = 0;
  avg_mem_power = 0;
  
  for (unsigned tmp_level = 0; ((int)tmp_level) < cycle ; ++tmp_level)
  {
    stats << tmp_level << "," ;
    power_stats << tmp_level << ",";
    //For FUs
    for (auto it = functionNames.begin(); it != functionNames.end() ; ++it)
    {
      stats << mul_activity[*it].at(tmp_level) << "," << add_activity[*it].at(tmp_level) << "," << bit_activity[*it].at(tmp_level) << ","; 
      float tmp_mul_power = (MUL_switch_power + MUL_int_power) * mul_activity[*it].at(tmp_level) + mul_leakage_per_cycle ;
      float tmp_add_power = (ADD_switch_power + ADD_int_power) * add_activity[*it].at(tmp_level) + add_leakage_per_cycle;
      avg_fu_power += tmp_mul_power + tmp_add_power;
      power_stats  << tmp_mul_power << "," << tmp_add_power << ",0," ;
    }
    //For memory
    for (auto it = partition_names.begin(); it != partition_names.end() ; ++it)
    {
      stats << ld_activity.at(*it).at(tmp_level) << "," << st_activity.at(*it).at(tmp_level) << "," ;
      float tmp_mem_power = scratchpad.readPower(*it) * ld_activity.at(*it).at(tmp_level) + 
                            scratchpad.writePower(*it) * st_activity.at(*it).at(tmp_level) + 
                            scratchpad.leakPower(*it);
      avg_mem_power += tmp_mem_power;
      power_stats << tmp_mem_power << "," ;
    }
    //For regs
    int curr_reg_reads = regStats.at(tmp_level).reads;
    int curr_reg_writes = regStats.at(tmp_level).writes;
    float tmp_reg_power = (REG_int_power + REG_sw_power) *(regStats.at(tmp_level).reads + regStats.at(tmp_level).writes) * 32  + reg_leakage_per_cycle;
    for (auto it = comp_partition_names.begin(); it != comp_partition_names.end() ; ++it)
    {
      curr_reg_reads     += ld_activity.at(*it).at(tmp_level);
      curr_reg_writes    += st_activity.at(*it).at(tmp_level);
      tmp_reg_power      += scratchpad.readPower(*it) * ld_activity.at(*it).at(tmp_level) + 
                            scratchpad.writePower(*it) * st_activity.at(*it).at(tmp_level) + 
                            scratchpad.leakPower(*it);
    }
    avg_fu_power += tmp_reg_power;
    
    stats << curr_reg_reads << "," << curr_reg_writes <<std::endl;
    power_stats << tmp_reg_power << std::endl;
  }
  stats.close();
  power_stats.close();
  
  avg_fu_power /= cycle;
  avg_mem_power /= cycle;
  avg_power = avg_fu_power + avg_mem_power;
  //Summary output:
  //Cycle, Avg Power, Avg FU Power, Avg MEM Power, Total Area, FU Area, MEM Area
  std::cerr << "===============================" << std::endl;
  std::cerr << "        Aladdin Results        " << std::endl;
  std::cerr << "===============================" << std::endl;
  std::cerr << "Running : " << benchName << std::endl;
  std::cerr << "Cycle : " << cycle << " cycle" << std::endl;
  std::cerr << "Avg Power: " << avg_power << " mW" << std::endl;
  std::cerr << "Avg FU Power: " << avg_fu_power << " mW" << std::endl;
  std::cerr << "Avg MEM Power: " << avg_mem_power << " mW" << std::endl;
  std::cerr << "Total Area: " << total_area << " uM^2" << std::endl;
  std::cerr << "FU Area: " << fu_area << " uM^2" << std::endl;
  std::cerr << "MEM Area: " << mem_area << " uM^2" << std::endl;
  std::cerr << "===============================" << std::endl;
  std::cerr << "        Aladdin Results        " << std::endl;
  std::cerr << "===============================" << std::endl;
  
  ofstream summary;
  tmp_name = bn + "_summary";
  summary.open(tmp_name.c_str());
  summary << "===============================" << std::endl;
  summary << "        Aladdin Results        " << std::endl;
  summary << "===============================" << std::endl;
  summary << "Running : " << benchName << std::endl;
  summary << "Cycle : " << cycle << " cycle" << std::endl;
  summary << "Avg Power: " << avg_power << " mW" << std::endl;
  summary << "Avg FU Power: " << avg_fu_power << " mW" << std::endl;
  summary << "Avg MEM Power: " << avg_mem_power << " mW" << std::endl;
  summary << "Total Area: " << total_area << " uM^2" << std::endl;
  summary << "FU Area: " << fu_area << " uM^2" << std::endl;
  summary << "MEM Area: " << mem_area << " uM^2" << std::endl;
  summary << "===============================" << std::endl;
  summary << "        Aladdin Results        " << std::endl;
  summary << "===============================" << std::endl;
  summary.close();
}

void Datapath::writeGlobalIsolated()
{
  std::string file_name(benchName);
  file_name += "_isolated.gz";
  write_gzip_bool_file(file_name, finalIsolated.size(), finalIsolated);
}
void Datapath::writeBaseAddress()
{
  ostringstream file_name;
  file_name << benchName << "_baseAddr.gz";
  gzFile gzip_file;
  gzip_file = gzopen(file_name.str().c_str(), "w");
  for (auto it = baseAddress.begin(), E = baseAddress.end(); it != E; ++it)
    gzprintf(gzip_file, "node:%u,part:%s,base:%lld\n", it->first, it->second.first.c_str(), it->second.second);
  gzclose(gzip_file);
}
void Datapath::writeFinalLevel()
{
  std::string file_name(benchName);
  file_name += "_level.gz";
  write_gzip_file(file_name, newLevel.size(), newLevel);
}
void Datapath::initMicroop(std::vector<int> &microop)
{
  std::string file_name(benchName);
  file_name += "_microop.gz";
  read_gzip_file(file_name, microop.size(), microop);
}
void Datapath::writeMicroop(std::vector<int> &microop)
{
  std::string file_name(benchName);
  file_name += "_microop.gz";
  write_gzip_file(file_name, microop.size(), microop);
}
void Datapath::initPrevBasicBlock(std::vector<std::string> &prevBasicBlock)
{
  std::string file_name(benchName);
  file_name += "_prevBasicBlock.gz";
  read_gzip_string_file(file_name, prevBasicBlock.size(), prevBasicBlock);
}
void Datapath::initDynamicMethodID(std::vector<std::string> &methodid)
{
  std::string file_name(benchName);
  file_name += "_dynamic_funcid.gz";
  read_gzip_string_file(file_name, methodid.size(), methodid);
}
void Datapath::initMethodID(std::vector<int> &methodid)
{
  std::string file_name(benchName);
  file_name += "_methodid.gz";
  read_gzip_file(file_name, methodid.size(), methodid);
}
void Datapath::initInstID(std::vector<std::string> &instid)
{
  std::string file_name(benchName);
  file_name += "_instid.gz";
  read_gzip_string_file(file_name, instid.size(), instid);
}
void Datapath::initAddress(std::unordered_map<unsigned, long long int> &address)
{
  std::string file_name(benchName);
  file_name += "_memaddr.gz";
  gzFile gzip_file;
  gzip_file = gzopen(file_name.c_str(), "r");
  while (!gzeof(gzip_file))
  {
    char buffer[256];
    if (gzgets(gzip_file, buffer, 256) == NULL)
      break;
    unsigned node_id, size;
    long long int addr;
    sscanf(buffer, "%d,%lld,%d\n", &node_id, &addr, &size);
    address[node_id] = addr;
  }
  gzclose(gzip_file);
}
void Datapath::initAddressAndSize(std::unordered_map<unsigned, pair<long long int, unsigned> > &address)
{
  std::string file_name(benchName);
  file_name += "_memaddr.gz";
  
  gzFile gzip_file;
  gzip_file = gzopen(file_name.c_str(), "r");
  while (!gzeof(gzip_file))
  {
    char buffer[256];
    if (gzgets(gzip_file, buffer, 256) == NULL)
      break;
    unsigned node_id, size;
    long long int addr;
    sscanf(buffer, "%d,%lld,%d\n", &node_id, &addr, &size);
    address[node_id] = make_pair(addr, size);
  }
  gzclose(gzip_file);
}

void Datapath::initializeGraphInMap(std::unordered_map<std::string, int> &full_graph)
{
  Graph tmp_graph;
  readGraph(tmp_graph); 
  unsigned num_of_edges = boost::num_edges(tmp_graph);
  
  VertexNameMap vertex_to_name = get(boost::vertex_name, tmp_graph);
  EdgeNameMap edge_to_name = get(boost::edge_name, tmp_graph);
  
  std::vector<int> edge_parid(num_of_edges, 0);

  initEdgeParID(edge_parid);
  
  //initialize full_graph
  edge_iter ei, ei_end;
  for (tie(ei, ei_end) = edges(tmp_graph); ei != ei_end; ++ei)
  {
    int edge_id = edge_to_name[*ei];
    int from = vertex_to_name[source(*ei, tmp_graph)];
    int to   = vertex_to_name[target(*ei, tmp_graph)];
    ostringstream oss;
    oss << from << "-" << to;
    full_graph[oss.str()] = edge_parid.at(edge_id);
  }
}

void Datapath::writeGraphInMap(std::unordered_map<std::string, int> &full_graph, std::string name)
{
  ofstream graph_file;
  gzFile new_edgeparid;
  
  std::string graph_name;
  std::string edge_parid_file;

  edge_parid_file = name + "_edgeparid.gz";
  graph_name = name + "_graph";
  
  new_edgeparid = gzopen(edge_parid_file.c_str(), "w");
  
  graph_file.open(graph_name.c_str());
  graph_file << "digraph DDDG {" << std::endl;
  for (unsigned node_id = 0; node_id < numTotalNodes; node_id++)
     graph_file << node_id << ";" << std::endl; 
  int new_edge_id = 0; 
  for(auto it = full_graph.begin(); it != full_graph.end(); it++)
  {
    int from, to;
    sscanf(it->first.c_str(), "%d-%d", &from, &to);
    
    graph_file << from << " -> " 
              << to
              << " [e_id = " << new_edge_id << "];" << std::endl;
    new_edge_id++;
    gzprintf(new_edgeparid, "%d\n", it->second);
  }
  graph_file << "}" << std::endl;
  graph_file.close();
  gzclose(new_edgeparid);
}

void Datapath::initLineNum(std::vector<int> &line_num)
{
  ostringstream file_name;
  file_name << benchName << "_linenum.gz";
  read_gzip_file(file_name.str(), line_num.size(), line_num);
}
void Datapath::initGetElementPtr(std::unordered_map<unsigned, pair<std::string, long long int> > &get_element_ptr)
{
  ostringstream file_name;
  file_name << benchName << "_getElementPtr.gz";
  gzFile gzip_file;
  gzip_file = gzopen(file_name.str().c_str(), "r");
  while (!gzeof(gzip_file))
  {
    char buffer[256];
    if (gzgets(gzip_file, buffer, 256) == NULL)
      break;
    unsigned node_id;
    long long int address;
    char label[256];
    sscanf(buffer, "%d,%[^,],%lld\n", &node_id, label, &address);
    get_element_ptr[node_id] = make_pair(label, address);
  }
  gzclose(gzip_file);
}
void Datapath::initEdgeParID(std::vector<int> &parid)
{
  std::string file_name(graphName);
  file_name += "_edgeparid.gz";
  read_gzip_file(file_name, parid.size(), parid);
}

//stepFunctions
//multiple function, each function is a separate graph
void Datapath::setGraphForStepping()
{
  std::cerr << "=============================================" << std::endl;
  std::cerr << "      Scheduling...            " << graphName << std::endl;
  std::cerr << "=============================================" << std::endl;
  
  newLevel.assign(numTotalNodes, 0);
  regStats.assign(numTotalNodes, {0, 0, 0});
  
  std::string gn(graphName);
  std::string graph_file_name(gn + "_graph");
  

  boost::dynamic_properties dp;
  boost::property_map<Graph, boost::vertex_name_t>::type v_name = get(boost::vertex_name, graph_);
  boost::property_map<Graph, boost::edge_name_t>::type e_name = get(boost::edge_name, graph_);
  dp.property("n_id", v_name);
  dp.property("e_id", e_name);
  std::ifstream fin(graph_file_name.c_str());
  boost::read_graphviz(fin, graph_, dp, "n_id");
  
  vertexToName = get(boost::vertex_name, graph_);
  edgeToName = get(boost::edge_name, graph_);
  BGL_FORALL_VERTICES(v, graph_, Graph)
    nameToVertex[get(boost::vertex_name, graph_, v)] = v;
  
  numTotalEdges  = boost::num_edges(graph_);
  edgeParid.assign(numTotalEdges, 0);
  initEdgeParID(edgeParid);

  numParents.assign(numTotalNodes, 0);
  latestParents.assign(numTotalNodes, 0);
  totalConnectedNodes = 0;
  vertex_iter vi, vi_end;

  for (tie(vi, vi_end) = vertices(graph_); vi != vi_end; ++vi)
  {
    if (boost::degree(*vi, graph_) == 0)
      finalIsolated.at(vertexToName[*vi]) = 1;
    else
    {
      numParents.at(vertexToName[*vi]) = boost::in_degree(*vi, graph_);
      totalConnectedNodes++;
    }
  }
  executedNodes = 0;
  
  executingQueue.clear();
  readyToExecuteQueue.clear();
  initExecutingQueue();
}

int Datapath::clearGraph()
{
  std::string gn(graphName);

  std::vector< Vertex > topo_nodes;
  boost::topological_sort(graph_, std::back_inserter(topo_nodes));
  //bottom nodes first
  std::vector<int> earliest_child(numTotalNodes, cycle);
  for (auto vi = topo_nodes.begin(); vi != topo_nodes.end(); ++vi)
  {
    unsigned node_id = vertexToName[*vi];
    if (finalIsolated.at(node_id))
      continue;
    unsigned node_microop = microop.at(node_id);
    if (!is_memory_op(node_microop) && ! is_branch_op(node_microop))
      if ((earliest_child.at(node_id) - 1 ) > newLevel.at(node_id))
        newLevel.at(node_id) = earliest_child.at(node_id) - 1;

    in_edge_iter in_i, in_end;
    for (tie(in_i, in_end) = in_edges(*vi , graph_); in_i != in_end; ++in_i)
    {
      int parent_id = vertexToName[source(*in_i, graph_)];
      if (earliest_child.at(parent_id) > newLevel.at(node_id))
        earliest_child.at(parent_id) = newLevel.at(node_id);
    }
  }
  updateRegStats();
  return cycle;
}
void Datapath::updateRegStats()
{
  std::map<int, Vertex> name_to_vertex;
  BGL_FORALL_VERTICES(v, graph_, Graph)
    name_to_vertex[get(boost::vertex_name, graph_, v)] = v;
  
  for(unsigned node_id = 0; node_id < numTotalNodes; node_id++)
  {
    if (finalIsolated.at(node_id))
      continue;
    if (is_control_op(microop.at(node_id)) || 
        is_index_op(microop.at(node_id)))
      continue;
    int node_level = newLevel.at(node_id);
    int max_children_level 		= node_level;
    
    Vertex node = name_to_vertex[node_id];
    out_edge_iter out_edge_it, out_edge_end;
    std::set<int> children_levels;
    for (tie(out_edge_it, out_edge_end) = out_edges(node, graph_); out_edge_it != out_edge_end; ++out_edge_it)
    {
      int child_id = vertexToName[target(*out_edge_it, graph_)];
      int child_microop = microop.at(child_id);
      if (is_control_op(child_microop))
        continue;
      
      if (is_load_op(child_microop)) 
        continue;
      
      int child_level = newLevel.at(child_id);
      if (child_level > max_children_level)
        max_children_level = child_level;
      if (child_level > node_level  && child_level != cycle - 1)
        children_levels.insert(child_level);
        
    }
    for (auto it = children_levels.begin(); it != children_levels.end(); it++)
        regStats.at(*it).reads++;
    
    if (max_children_level > node_level && node_level != 0 )
      regStats.at(node_level).writes++;
  }
}
void Datapath::copyToExecutingQueue()
{
  auto it = readyToExecuteQueue.begin(); 
  while (it != readyToExecuteQueue.end())
  {
    executingQueue.push_back(*it);
    it = readyToExecuteQueue.erase(it);
  }
}
void Datapath::step()
{
  stepExecutingQueue();
  copyToExecutingQueue();
  DPRINTF(Datapath, "Aladdin stepping @ Cycle:%d, executed:%d, total:%d\n", cycle, executedNodes, totalConnectedNodes);
  cycle++;
  //FIXME: exit condition
  if (executedNodes < totalConnectedNodes)
  {
    scratchpad.step();
    schedule(tickEvent, clockEdge(Cycles(1)));
  }
}

void Datapath::stepExecutingQueue()
{
  auto it = executingQueue.begin();
  int index = 0;
  while (it != executingQueue.end())
  {
    unsigned node_id = *it;
    if (is_memory_op(microop.at(node_id)))
    {
      std::string node_part = baseAddress[node_id].first;
      if(scratchpad.canServicePartition(node_part))
      {
        assert(scratchpad.addressRequest(node_part));
        executedNodes++;
        newLevel.at(node_id) = cycle;
        executingQueue.erase(it);
        updateChildren(node_id);
        it = executingQueue.begin();
        std::advance(it, index);
      }
      else
      {
        ++it;
        ++index;
      }
    }
    else
    {
      executedNodes++;
      newLevel.at(node_id) = cycle;
      executingQueue.erase(it);
      updateChildren(node_id);
      it = executingQueue.begin();
      std::advance(it, index);
    }
  }
}
void Datapath::updateChildren(unsigned node_id)
{
  Vertex node = nameToVertex[node_id];
  out_edge_iter out_edge_it, out_edge_end;
  for (tie(out_edge_it, out_edge_end) = out_edges(node, graph_); out_edge_it != out_edge_end; ++out_edge_it)
  {
    unsigned child_id = vertexToName[target(*out_edge_it, graph_)];
    unsigned edge_id = edgeToName[*out_edge_it];
    if (numParents[child_id] > 0)
    {
      numParents[child_id]--;
      if (numParents[child_id] == 0)
      {
        unsigned child_microop = microop.at(child_id);
        if ( (node_latency(child_microop) == 0 || node_latency(microop.at(node_id))== 0)
             && edgeParid[edge_id] != CONTROL_EDGE )
          executingQueue.push_back(child_id);
        else
          readyToExecuteQueue.push_back(child_id);
        numParents[child_id] = -1;
      }
    }
  }
}

void Datapath::initExecutingQueue()
{
  for(unsigned i = 0; i < numTotalNodes; i++)
  {
    if (numParents[i] == 0 && finalIsolated[i] != 1)
      executingQueue.push_back(i);
  }
}

//readConfigs
bool Datapath::readPipeliningConfig()
{
  ifstream config_file;
  std::string file_name(benchName);
  file_name += "_pipelining_config";
  config_file.open(file_name.c_str());
  if (!config_file.is_open())
    return 0;
  std::string wholeline;
  getline(config_file, wholeline);
  if (wholeline.size() == 0)
    return 0;
  bool flag = atoi(wholeline.c_str());
  return flag;
}

bool Datapath::readUnrollingConfig(std::unordered_map<int, int > &unrolling_config)
{
  ifstream config_file;
  std::string file_name(benchName);
  file_name += "_unrolling_config";
  config_file.open(file_name.c_str());
  if (!config_file.is_open())
    return 0;
  while(!config_file.eof())
  {
    std::string wholeline;
    getline(config_file, wholeline);
    if (wholeline.size() == 0)
      break;
    char func[256];
    int line_num, factor;
    sscanf(wholeline.c_str(), "%[^,],%d,%d\n", func, &line_num, &factor);
    unrolling_config[line_num] =factor;
  }
  config_file.close();
  return 1;
}

bool Datapath::readFlattenConfig(std::unordered_set<int> &flatten_config)
{
  ifstream config_file;
  std::string file_name(benchName);
  file_name += "_flatten_config";
  config_file.open(file_name.c_str());
  if (!config_file.is_open())
    return 0;
  while(!config_file.eof())
  {
    std::string wholeline;
    getline(config_file, wholeline);
    if (wholeline.size() == 0)
      break;
    char func[256];
    int line_num;
    sscanf(wholeline.c_str(), "%[^,],%d\n", func, &line_num);
    flatten_config.insert(line_num);
  }
  config_file.close();
  return 1;
}


bool Datapath::readCompletePartitionConfig(std::unordered_map<std::string, unsigned> &config)
{
  std::string bn(benchName);
  std::string comp_partition_file;
  comp_partition_file = bn + "_complete_partition_config";
  
  if (!fileExists(comp_partition_file))
    return 0;
  
  ifstream config_file;
  config_file.open(comp_partition_file);
  std::string wholeline;
  while(!config_file.eof())
  {
    getline(config_file, wholeline);
    if (wholeline.size() == 0)
      break;
    unsigned size;
    char type[256];
    char base_addr[256];
    sscanf(wholeline.c_str(), "%[^,],%[^,],%d\n", type, base_addr, &size);
    config[base_addr] = size;
  }
  config_file.close();
  return 1;
}

bool Datapath::readPartitionConfig(std::unordered_map<std::string, partitionEntry> & partition_config)
{
  ifstream config_file;
  std::string file_name(benchName);
  file_name += "_partition_config";
  if (!fileExists(file_name))
    return 0;

  config_file.open(file_name.c_str());
  std::string wholeline;
  while (!config_file.eof())
  {
    getline(config_file, wholeline);
    if (wholeline.size() == 0) break;
    unsigned size, p_factor;
    char type[256];
    char base_addr[256];
    sscanf(wholeline.c_str(), "%[^,],%[^,],%d,%d,\n", type, base_addr, &size, &p_factor);
    std::string p_type(type);
    partition_config[base_addr] = {p_type, size, p_factor};
  }
  config_file.close();
  return 1;
}

void Datapath::parse_config()
{
  ifstream config_file;
  config_file.open(configFileName);
  std::string wholeline;

  std::vector<std::string> flatten_config;
  std::vector<std::string> unrolling_config;
  std::vector<std::string> partition_config;
  std::vector<std::string> comp_partition_config;
  std::vector<std::string> pipelining_config;

  while(!config_file.eof())
  {
    wholeline.clear();
    getline(config_file, wholeline);
    if (wholeline.size() == 0)
      break;
    string type, rest_line;
    int pos_end_tag = wholeline.find(",");
    if (pos_end_tag == -1)
      break;
    type = wholeline.substr(0, pos_end_tag);
    rest_line = wholeline.substr(pos_end_tag + 1);
    if (!type.compare("flatten"))
      flatten_config.push_back(rest_line); 

    else if (!type.compare("unrolling"))
      unrolling_config.push_back(rest_line); 

    else if (!type.compare("partition"))
      if (wholeline.find("complete") == std::string::npos)
        partition_config.push_back(rest_line); 
      else 
        comp_partition_config.push_back(rest_line); 
    else if (!type.compare("pipelining"))
      pipelining_config.push_back(rest_line);
    else
    {
      cerr << "what else? " << wholeline << std::endl;
      exit(0);
    }
  }
  config_file.close();
  if (flatten_config.size() != 0)
  {
    string file_name(benchName);
    file_name += "_flatten_config";
    ofstream output;
    output.open(file_name);
    for (unsigned i = 0; i < flatten_config.size(); ++i)
      output << flatten_config.at(i) << std::endl;
    output.close();
  }
  if (unrolling_config.size() != 0)
  {
    string file_name(benchName);
    file_name += "_unrolling_config";
    ofstream output;
    output.open(file_name);
    for (unsigned i = 0; i < unrolling_config.size(); ++i)
      output << unrolling_config.at(i) << std::endl;
    output.close();
  }
  if (pipelining_config.size() != 0)
  {
    string pipelining(benchName);
    pipelining += "_pipelining_config";

    ofstream pipe_config;
    pipe_config.open(pipelining);
    for (unsigned i = 0; i < pipelining_config.size(); ++i)
      pipe_config << pipelining_config.at(i) << std::endl;
    pipe_config.close();
  }
  if (partition_config.size() != 0)
  {
    string partition(benchName);
    partition += "_partition_config";

    ofstream part_config;
    part_config.open(partition);
    for (unsigned i = 0; i < partition_config.size(); ++i)
      part_config << partition_config.at(i) << std::endl;
    part_config.close();
  }
  if (comp_partition_config.size() != 0)
  {
    string complete_partition(benchName);
    complete_partition += "_complete_partition_config";

    ofstream comp_config;
    comp_config.open(complete_partition);
    for (unsigned i = 0; i < comp_partition_config.size(); ++i)
      comp_config << comp_partition_config.at(i) << std::endl;
    comp_config.close();
  }
}

////////////////////////////////////////////////////////////////////////////
//
//  The SimObjects we use to get the Datapath information into the simulator
//
////////////////////////////////////////////////////////////////////////////

Datapath *
DatapathParams::create()
{
  return new Datapath(this);
}


