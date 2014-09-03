#ifndef FILE_FUNC_H
#define FILE_FUNC_H

#include <iostream> 
#include <sstream> // std::istringstream
#include <fstream>
#include <stdlib.h>
#include <string> 
#include <vector>
#include <unordered_map>
#include <assert.h>
#include <sys/stat.h>
#include <zlib.h>
using namespace std;

void write_gzip_file(string gzip_file_name, unsigned size, vector<int> &output);
void write_gzip_bool_file(string gzip_file_name, unsigned size, vector<bool> &output);
void write_gzip_unsigned_file(string gzip_file_name, unsigned size, vector<unsigned> &output);
void write_gzip_string_file(string gzip_file_name, unsigned size, vector<string> &output);
void write_string_file(string file_name, unsigned size, vector<string> &output);

void read_file(string file_name, vector<int> &output);
void read_gzip_file(string gzip_file_name, unsigned size, vector<int> &output);
void read_gzip_unsigned_file(string gzip_file_name, unsigned size, vector<unsigned> &output);
void read_gzip_string_file ( string gzip_file_name, unsigned size, vector<string> &output);
void read_gzip_file_no_size(string gzip_file_name, vector<int> &output);
void read_gzip_2_unsigned_file(string gzip_file_name, unsigned size, vector< pair<unsigned, unsigned> > &output);
void read_gzip_1in2_unsigned_file(string gzip_file_name, unsigned size, vector<unsigned> &output);

bool fileExists(const string file_name);
#endif
