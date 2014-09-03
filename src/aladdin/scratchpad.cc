#include "scratchpad.hh"

const unsigned MEM_size[]  = {64, 128, 256, 512, 1024, 2049, 4098, 8196, 16392, 32784, 65568, 131136, 262272, 524288, 1048576, 2097152, 4194304};
const float MEM_rd_power[] = {1.779210, 1.779210, 2.653500, 2.653500, 3.569050, 4.695780, 5.883620, 7.587260, 9.458480, 8.363850, 13.472600, 12.640600, 18.336900, 14.724300, 23.883600, 16.310100, 28.517300};

const float MEM_wr_power[] = {1.733467, 1.733467, 2.531965, 2.531965, 3.138079, 3.783919, 4.450720, 5.007659, 5.370660, 4.590109, 7.371770, 5.849070, 6.549049, 4.353763, 5.253279, 2.894534, 3.630445};
const float MEM_lk_power[] = {0.013156, 0.026312, 0.052599, 0.105198, 0.210474, 0.420818, 0.841640, 1.682850, 3.365650, 6.729040, 13.459700, 26.916200, 53.832100, 107.658000, 215.316000, 430.620000, 861.240000};
const float MEM_area[]     = {1616.140000, 2929.000000, 4228.290000, 7935.990000, 15090.200000, 28129.300000, 49709.900000, 94523.900000, 174459.000000, 352194.000000, 684305.000000, 1319220.000000, 2554980.000000, 5167380.000000, 9861550.000000, 19218800.00000, 37795600.00000};


Scratchpad::Scratchpad(Datapath *_datapath, unsigned p_ports_per_part)
  : datapath(_datapath),
    numOfPortsPerPartition(p_ports_per_part){}

Scratchpad::~Scratchpad()
{}

void Scratchpad::setCompScratchpad(std::string baseName, unsigned size)
{
  assert(!partitionExist(baseName));
  //size: number of words
  unsigned new_id = baseToPartitionID.size();
  baseToPartitionID[baseName] = new_id;
  
  compPartition.push_back(true);
  occupiedBWPerPartition.push_back(0);
  sizePerPartition.push_back(size);
  //set
  readPowerPerPartition.push_back(size * 32 * (REG_int_power + REG_sw_power));
  writePowerPerPartition.push_back(size * 32 * (REG_int_power + REG_sw_power));
  leakPowerPerPartition.push_back(size * 32 * REG_leak_power);
  areaPerPartition.push_back(size * 32 * REG_area);
}

void Scratchpad::setScratchpad(std::string baseName, unsigned size)
{
  assert(!partitionExist(baseName));
  //size: number of words
  unsigned new_id = baseToPartitionID.size();
  baseToPartitionID[baseName] = new_id;
  
  compPartition.push_back(false);
  occupiedBWPerPartition.push_back(0);
  sizePerPartition.push_back(size);
  //set
  unsigned mem_size = next_power_of_two(size);
  if (mem_size < 64)
    mem_size = 64;
  if (mem_size  > 4194304 )
  {
    std::cerr << "Error: Scratchpad cannot be larger than 4M words" << std::endl;
    exit(0);
  }
  unsigned mem_index = (unsigned) log2(mem_size) - 6;
  readPowerPerPartition.push_back(MEM_rd_power[mem_index]);
  writePowerPerPartition.push_back(MEM_wr_power[mem_index]);
  leakPowerPerPartition.push_back(MEM_lk_power[mem_index]);
  areaPerPartition.push_back(MEM_area[mem_index]);
}

void Scratchpad::step()
{ 
  for (auto it = occupiedBWPerPartition.begin(); it != occupiedBWPerPartition.end(); ++it){
    *it = 0;
}
}
bool Scratchpad::partitionExist(std::string baseName)
{
  auto partition_it = baseToPartitionID.find(baseName);
  if (partition_it != baseToPartitionID.end())
    return 1;
  else
    return 0;
}
bool Scratchpad::addressRequest(std::string baseName)
{
  if (canServicePartition(baseName))
  {
    unsigned partition_id = findPartitionID(baseName);
    if (compPartition.at(partition_id))
      return true;
    assert(occupiedBWPerPartition.at(partition_id) < numOfPortsPerPartition);
    occupiedBWPerPartition.at(partition_id)++;
    return true;
  }
  else
    return false;
}
bool Scratchpad::canService()
{
  for(auto base_it = baseToPartitionID.begin(); base_it != baseToPartitionID.end(); ++base_it)
  {
    std::string base_name = base_it->first;
    //unsigned base_id = base_it->second;
    //if (!compPartition.at(base_id))
      if (canServicePartition(base_name))
        return 1;
  }
  return 0;
}
bool Scratchpad::canServicePartition(std::string baseName)
{
  unsigned partition_id = findPartitionID(baseName);
  if (compPartition.at(partition_id))
    return 1;
  else if (occupiedBWPerPartition.at(partition_id) < numOfPortsPerPartition)
    return 1;
  else
    return 0;
}

unsigned Scratchpad::findPartitionID(std::string baseName)
{
  
  auto partition_it = baseToPartitionID.find(baseName);
  if (partition_it != baseToPartitionID.end())
    return partition_it->second;
  else
  {
    std::cerr << "Unknown Partition Name:" << baseName << std::endl;
    std::cerr << "Need to Explicitly Declare How to Partition Each Array in the Config File" << std::endl;
    exit(0);
  }
}

void Scratchpad::partitionNames(std::vector<std::string> &names)
{
  for(auto base_it = baseToPartitionID.begin(); base_it != baseToPartitionID.end(); ++base_it)
  {
    std::string base_name = base_it->first;
    unsigned base_id = base_it->second;
    if (!compPartition.at(base_id))
      names.push_back(base_name);
  }
}

void Scratchpad::compPartitionNames(std::vector<std::string> &names)
{
  for(auto base_it = baseToPartitionID.begin(); base_it != baseToPartitionID.end(); ++base_it)
  {
    std::string base_name = base_it->first;
    unsigned base_id = base_it->second;
    if (compPartition.at(base_id))
      names.push_back(base_name);
  }
}

float Scratchpad::readPower (std::string baseName)
{
  unsigned partition_id = findPartitionID(baseName);
  return readPowerPerPartition.at(partition_id);
}

float Scratchpad::writePower (std::string baseName)
{
  unsigned partition_id = findPartitionID(baseName);
  return writePowerPerPartition.at(partition_id);
}

float Scratchpad::leakPower (std::string baseName)
{
  unsigned partition_id = findPartitionID(baseName);
  return leakPowerPerPartition.at(partition_id);
}

float Scratchpad::area (std::string baseName)
{
  unsigned partition_id = findPartitionID(baseName);
  return areaPerPartition.at(partition_id);
}

