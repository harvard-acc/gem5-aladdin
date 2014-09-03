#ifndef __DATAPATH_HH__
#define __DATAPATH_HH__

//gem5 Headers
#include "base/types.hh"
#include "base/trace.hh"
#include "mem/mem_object.hh"
#include "sim/clocked_object.hh"
#include "params/Datapath.hh"

//ALaddin Headers
#include <boost/graph/graphviz.hpp>
#include <boost/config.hpp>
#include <boost/graph/adjacency_list.hpp>
#include <boost/graph/graph_traits.hpp>
#include <boost/graph/properties.hpp>
#include <boost/graph/topological_sort.hpp>
#include <boost/graph/iteration_macros.hpp>
#include <iostream>
#include <assert.h>
#include <unordered_map>
#include <unordered_set>
#include <algorithm>
#include <map>
#include <set>
#include "file_func.h"
#include "opcode_func.h"
#include "generic_func.h"

#include "scratchpad.hh"
#include "dddg.hh"

#define CONTROL_EDGE 11
#define PIPE_EDGE 12

using namespace std;
typedef boost::property < boost::vertex_name_t, int> VertexProperty;
typedef boost::property < boost::edge_name_t, int> EdgeProperty;
typedef boost::adjacency_list < boost::vecS, boost::vecS, boost::bidirectionalS, VertexProperty, EdgeProperty> Graph;
typedef boost::graph_traits<Graph>::vertex_descriptor Vertex;
typedef boost::graph_traits<Graph>::edge_descriptor Edge;
typedef boost::graph_traits<Graph>::vertex_iterator vertex_iter;
typedef boost::graph_traits<Graph>::edge_iterator edge_iter;
typedef boost::graph_traits<Graph>::in_edge_iterator in_edge_iter;
typedef boost::graph_traits<Graph>::out_edge_iterator out_edge_iter;
typedef boost::property_map<Graph, boost::edge_name_t>::type EdgeNameMap;
typedef boost::property_map<Graph, boost::vertex_name_t>::type VertexNameMap;
typedef boost::property_map<Graph, boost::vertex_index_t>::type VertexIndexMap;

class Datapath: public MemObject
{
  private:
    
    struct partitionEntry
    {
      std::string type;
      unsigned array_size; //num of elements
      unsigned part_factor;
    };
    struct regEntry
    {
      int size;
      int reads;
      int writes;
    };
    struct callDep
    {
      std::string caller;
      std::string callee;
      int callInstID;
    };
    struct newEdge
    {
      unsigned from;
      unsigned to;
      int parid;
    };
    struct RQEntry
    {
      unsigned node_id;
      mutable float latency_so_far;
      mutable bool valid;
    };

    struct RQEntryComp
    {
      bool operator() (const RQEntry& left, const RQEntry &right) const  
      { return left.node_id < right.node_id; }
    };
  public:
    typedef DatapathParams Params;
    std::string benchName;
    std::string traceFileName;
    std::string configFileName;
    float cycleTime;
    
    DDDG dddg;
    Scratchpad scratchpad;
    
    //gem5 tick
    void step();
    EventWrapper<Datapath, &Datapath::step> tickEvent;
    
    Datapath(const Params *p);
    ~Datapath();
    
    void parse_config();
    bool fileExists (const string file_name)
    {
      struct stat buf;
      if (stat(file_name.c_str(), &buf) != -1)
        return true;
      return false;
    }

    std::string getBenchName() { return benchName; }
    std::string getTraceFileName(){return traceFileName;}
    std::string getConfigFileName() {return configFileName;}
    void setGlobalGraph();
    void clearGlobalGraph();
    void globalOptimizationPass();
    void initBaseAddress();
    void cleanLeafNodes();
    void completePartition();
    void removeInductionDependence();
    void removePhiNodes();
    void memoryAmbiguation();
    void removeAddressCalculation();
    void removeBranchEdges();
    void nodeStrengthReduction();
    void loopFlatten();
    void loopUnrolling();
    void loopPipelining();
    void removeSharedLoads();
    void storeBuffer();
    void removeRepeatedStores();
    void treeHeightReduction();
    void scratchpadPartition();
    void findMinRankNodes(unsigned &node1, unsigned &node2, std::map<unsigned, unsigned> &rank_map);

    bool readPipeliningConfig();
    bool readUnrollingConfig(std::unordered_map<int, int > &unrolling_config);
    bool readFlattenConfig(std::unordered_set<int> &flatten_config);
    bool readPartitionConfig(std::unordered_map<std::string,
           partitionEntry> & partition_config);
    bool readCompletePartitionConfig(std::unordered_map<std::string, unsigned> &config);

    /*void readGraph(igraph_t *g);*/
    void dumpStats();
    void writeFinalLevel();
    void writeGlobalIsolated();
    void writePerCycleActivity();
    void writeBaseAddress();
    void writeMicroop(std::vector<int> &microop);
    
    void readGraph(Graph &g);
    void initMicroop(std::vector<int> &microop);
    void updateRegStats();
    void initMethodID(std::vector<int> &methodid);
    void initDynamicMethodID(std::vector<std::string> &methodid);
    void initPrevBasicBlock(std::vector<std::string> &prevBasicBlock);
    void initInstID(std::vector<std::string> &instid);
    void initAddressAndSize(std::unordered_map<unsigned, pair<long long int, unsigned> > &address);
    void initAddress(std::unordered_map<unsigned, long long int> &address);
    void initLineNum(std::vector<int> &lineNum);
    void initEdgeParID(std::vector<int> &parid);
    void initGetElementPtr(std::unordered_map<unsigned, pair<std::string, long long int> > &get_element_ptr);

    int writeGraphWithIsolatedEdges(std::vector<bool> &to_remove_edges);
    int writeGraphWithNewEdges(std::vector<newEdge> &to_add_edges, int curr_num_of_edges);
    int writeGraphWithIsolatedNodes(std::unordered_set<unsigned> &to_remove_nodes);
    void writeGraphInMap(std::unordered_map<std::string, int> &full_graph, std::string name);
    void initializeGraphInMap(std::unordered_map<std::string, int> &full_graph);

    void setGraphForStepping();
    
    void stepExecutingQueue();
    void updateChildren(unsigned node_id);
    void copyToExecutingQueue();
    int fireMemNodes();
    int fireNonMemNodes();
    void initExecutingQueue();
    void addMemReadyNode( unsigned node_id, float latency_so_far);
    void addNonMemReadyNode( unsigned node_id, float latency_so_far);
    int clearGraph();

  private:
    
    
    
    //global/whole datapath variables
    std::vector<int> newLevel;
    std::vector<regEntry> regStats;
    std::vector<int> microop;
    std::unordered_map<unsigned, pair<std::string, long long int> > baseAddress;

    unsigned numTotalNodes;
    unsigned numTotalEdges;

    //stateful states
    int cycle;
    
    //local/per method variables for step(), may need to include new data
    //structure for optimization phase
    std::string graphName;
    
    /*igraph_t *g;*/
    /*Graph global_graph_;*/
    Graph graph_;
    std::unordered_map<int, Vertex> nameToVertex;
    VertexNameMap vertexToName;
    EdgeNameMap edgeToName;

    std::vector<int> numParents;
    std::vector<int> edgeParid;
    std::vector<float> latestParents;
    std::vector<bool> finalIsolated;
    std::vector<int> edgeLatency;
    
    std::unordered_set<std::string> dynamicMemoryOps;
    std::unordered_set<std::string> functionNames;
    
    //stateful states
    unsigned totalConnectedNodes;
    unsigned executedNodes;
    
    std::vector<unsigned> executingQueue;
    std::vector<unsigned> readyToExecuteQueue;

};
#endif
