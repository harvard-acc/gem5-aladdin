#ifndef __SCRATCHPAD_HH__
#define __SCRATCHPAD_HH__

#include <iostream>
#include <assert.h>
#include <unordered_map>
#include <vector>
#include <math.h>

#include "power_delay.h"
#include "generic_func.h"

class Datapath;

class Scratchpad
{
private:
  Datapath *datapath;
  unsigned numOfPortsPerPartition;
public:
  Scratchpad(Datapath *_datapath, unsigned p_ports_per_part);
  ~Scratchpad();
  void step();
  void setScratchpad(std::string baseName, unsigned size);
  void setCompScratchpad(std::string baseName, unsigned size);
  bool canService();
  bool canServicePartition(std::string baseName);
  bool partitionExist(std::string baseName);
  unsigned findPartitionID(std::string baseName);
  bool addressRequest(std::string baseName);
  void partitionNames(std::vector<std::string> &names);
  void compPartitionNames(std::vector<std::string> &names);
  float readPower(std::string baseName);
  float writePower(std::string baseName);
  float leakPower(std::string baseName);
  float area(std::string baseName);

private:

  unsigned numOfPartitions;
  std::unordered_map<std::string, unsigned> baseToPartitionID;
  
  std::vector<bool> compPartition;
  std::vector<unsigned> occupiedBWPerPartition;
  std::vector<unsigned> sizePerPartition;
  std::vector<float> readPowerPerPartition;
  std::vector<float> writePowerPerPartition;
  std::vector<float> leakPowerPerPartition;
  std::vector<float> areaPerPartition;
};

#endif
