#include "file_func.h"

void read_file(string file_name, vector<int> &output)
{
  ifstream file;
#ifdef DDEBUG
  std::cerr << file_name << std::endl;
#endif
  file.open(file_name.c_str());
  string wholeline;
  if (file.is_open())
  {
    while (!file.eof())
    {
      getline(file, wholeline);
      if (wholeline.size() == 0)
        break;
      output.push_back(atoi(wholeline.c_str()));
    }
    file.close();
  }
  else
  {
    std::cerr << "file not open " << file_name << std::endl;
    exit(0);
  }
}

void read_gzip_string_file ( string gzip_file_name, unsigned size,
  vector<string> &output)
{
  gzFile gzip_file;
  gzip_file = gzopen(gzip_file_name.c_str(), "r");

#ifdef DDEBUG
  std::cerr << gzip_file_name << std::endl;
#endif
  unsigned i = 0;
  while(!gzeof(gzip_file) && i< size)
  {
    char buffer[256];
    gzgets(gzip_file, buffer, 256);
    string s(buffer);
    output.at(i) = s.substr(0,s.size()-1);
    i++;
  }
  gzclose(gzip_file);
  if (i == 0)
  {
    std::cerr << "file not open " << gzip_file_name << std::endl;
    exit(0);
  }
}

/*Read gz file into vector
Input: gzip-file-name, size of elements, vector to write to
*/
void read_gzip_file(string gzip_file_name, unsigned size, vector<int> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << std::endl;
#endif
  if (fileExists(gzip_file_name))
  {
    gzip_file = gzopen(gzip_file_name.c_str(), "r");
    unsigned i = 0;
    while(!gzeof(gzip_file) && i< size)
    {
      char buffer[256];
      gzgets(gzip_file, buffer, 256);
      output.at(i) = strtol(buffer, NULL, 10);
      i++;
    }
    gzclose(gzip_file);
    
#ifdef DDEBUG
    std::cerr << "end of reading file" << gzip_file_name << std::endl;
#endif
  }
  else
  {
    std::cerr << "no such file " <<  gzip_file_name << std::endl;
  }
}
void read_gzip_unsigned_file(string gzip_file_name, unsigned size,
  vector<unsigned> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << std::endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "r");
  unsigned i = 0;
  while(!gzeof(gzip_file) && i< size)
  {
    char buffer[256];
    gzgets(gzip_file, buffer, 256);
    output.at(i) = (unsigned)strtol(buffer, NULL, 10);
    i++;
  }
  gzclose(gzip_file);
}

/*Read gz file into vector
Input: gzip-file-name, size of elements, vector to write to
*/
void read_gzip_file_no_size(string gzip_file_name, vector<int> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << std::endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "r");
  unsigned i = 0;
  while(!gzeof(gzip_file) )
  {
    char buffer[256];
    gzgets(gzip_file, buffer, 256);
    int value;
    sscanf(buffer, "%d", &value);
    output.push_back(value);
    i++;
  }
  gzclose(gzip_file);
  
  if (i == 0)
  {
    std::cerr << "file not open " << gzip_file_name << std::endl;
    exit(0);
  }
}

/*Read gz with two element file into vector
Input: gzip-file-name, size of elements, vector to write to
*/
void read_gzip_2_unsigned_file(string gzip_file_name, unsigned size,
  vector< pair<unsigned, unsigned> > &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << std::endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "r");
  unsigned i = 0;
  while(!gzeof(gzip_file) && i< size)
  {
    char buffer[256];
    gzgets(gzip_file, buffer, 256);
    unsigned element1, element2;
    sscanf(buffer, "%d,%d", &element1, &element2);
    output.at(i).first = element1;
    output.at(i).second = element2;
    i++;
  }
  gzclose(gzip_file);
  
  if (i == 0)
  {
    std::cerr << "file not open " << gzip_file_name << std::endl;
    exit(0);
  }
}

void read_gzip_1in2_unsigned_file(string gzip_file_name, unsigned size,
  vector<unsigned> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << "," << size << std::endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "r");
  unsigned i = 0;
  while(!gzeof(gzip_file) && i< size)
  {
    char buffer[256];
    gzgets(gzip_file, buffer, 256);
    unsigned element1, element2;
    sscanf(buffer, "%d,%d", &element1, &element2);
    output.at(i) = element1;
    i++;
  }
  gzclose(gzip_file);
  if (i == 0)
  {
    std::cerr << "file not open " << gzip_file_name << endl;
    exit(0);
  }
}

void write_gzip_file(string gzip_file_name, unsigned size, vector<int> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "w");
  for (unsigned i = 0; i < size; ++i)
    gzprintf(gzip_file, "%d\n", output.at(i));
  gzclose(gzip_file);
}

void write_gzip_bool_file(std::string gzip_file_name, unsigned size, std::vector<bool> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "w");
  for (unsigned i = 0; i < size; ++i)
    gzprintf(gzip_file, "%d\n", output.at(i) ? 1 : 0);
  gzclose(gzip_file);
}

void write_gzip_unsigned_file(string gzip_file_name, unsigned size,
vector<unsigned> &output)
{
  gzFile gzip_file;
#ifdef DDEBUG
  std::cerr << gzip_file_name << endl;
#endif
  gzip_file = gzopen(gzip_file_name.c_str(), "w");
  for (unsigned i = 0; i < size; ++i)
    gzprintf(gzip_file, "%u\n", output.at(i));
  gzclose(gzip_file);
}

void write_string_file(string file_name, unsigned size, vector<string> &output)
{
  ofstream file;
  file.open(file_name.c_str());
  for (unsigned i = 0; i < size; ++i)
    file << output.at(i) << endl;
  file.close();
}

void write_gzip_string_file(string gzip_file_name, unsigned size, vector<string> &output)
{
  gzFile gzip_file;
  gzip_file = gzopen(gzip_file_name.c_str(), "w");
#ifdef DDEBUG
  std::cerr << gzip_file_name << endl;
#endif
  for (unsigned i = 0; i < size; ++i)
    gzprintf(gzip_file, "%s\n", output.at(i).c_str());
  gzclose(gzip_file);
}

bool fileExists (const string file_name)
{
  struct stat buf;
  if (stat(file_name.c_str(), &buf) != -1)
    return true;
  return false;
}

