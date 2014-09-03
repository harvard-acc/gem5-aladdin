#include "generic_func.h"

std::vector<int> make_vector(int size)
{
  std::vector<int> tmp_vector(size, 0);
  return tmp_vector;
}
int max_value(const vector<int> array, int start, int end)
{
  int max = array.at(start);
  for (int i = start; i < end; ++i)
  {
    if (array.at(i) > max)
      max = array.at(i);
  }
  return max;
}

int min_value(const vector<int> array, int start, int end)
{
  int min = array.at(start);
  for (int i = start; i < end; ++i)
  {
    if (array.at(i) < min)
      min = array.at(i);
  }
  return min;
}
unsigned next_power_of_two(unsigned int x) 
{
  x = x - 1; 
  x = x | (x >> 1); 
  x = x | (x >> 2); 
  x = x | (x >> 4); 
  x = x | (x >> 8); 
  x = x | (x >> 16); 
  return x + 1; 
} 
