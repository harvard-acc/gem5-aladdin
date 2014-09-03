#ifndef GENERIC_FUNC_H
#define GENERIC_FUNC_H
#include <vector>
using namespace std;
std::vector<int> make_vector(int size);
int max_value(const vector<int> array, int start, int end);

int min_value(const vector<int> array, int start, int end);
unsigned next_power_of_two(unsigned int x);
#endif
