#include <cstring>
#include <iosfwd>
#include <iostream>
#include <string>
#include <sstream>

#include "base/logging.hh"
#include "base/stats/info.hh"
#include "base/stats/output.hh"

#include "sql.hh"

#ifdef ENABLE_SQLITE_STATS_OUTPUT

#include <sqlite3.h>

namespace Stats {

OutputSQL::OutputSQL() : db(nullptr), tables_created(false), dump_count(0) {}

OutputSQL::OutputSQL(const std::string &filename)
    : tables_created(false), dump_count(0) {
  open(filename);
}

OutputSQL::~OutputSQL() {
  if (db) {
    int close_ret = sqlite3_close(db);
    if (close_ret != SQLITE_OK)
      print_errmsg(close_ret);
    db = nullptr;
  }
}

void OutputSQL::open(const std::string &filename) {
  if (db)
    panic("Database has already been opened!\n");

  int ret = sqlite3_open(filename.c_str(), &db);
  if (ret != SQLITE_OK) {
    print_errmsg(ret);
    ret = sqlite3_close(db);
    if (ret != SQLITE_OK)
      print_errmsg(ret);
    db = nullptr;
  } else {
    tables_created = create_tables();
  }

  if (!valid())
    fatal("Unable to write to the statistics database\n");
}

int OutputSQL::exec_sql(const std::string& sql_cmd) {
  char* errmsg;
  int ret = sqlite3_exec(db, sql_cmd.c_str(), nullptr, nullptr, &errmsg);
  if (ret != SQLITE_OK) {
    print_errmsg(ret, errmsg);
    sqlite3_free(errmsg);
  }
  return ret;
}

bool OutputSQL::valid() const {
  return (db != nullptr && tables_created);
}

void OutputSQL::print_errmsg(int code, char *errmsg) {
  std::cerr << "[SQLITE3 ERROR]: " << code << " ";
  if (errmsg)
    std::cerr << errmsg;
  else
    std::cerr << sqlite3_errmsg(db);
  std::cerr << std::endl;
}

bool OutputSQL::create_tables() {
  std::string stats_sql =
      "drop table if exists stats;"
      "create table stats ("
          "id int primary key,"
          "name text,"
          "desc text,"
          "subnames text,"
          "y_subnames text,"
          "subdescs text,"
          "precision int,"
          "prereq int,"
          "flags int,"
          "x int,"
          "y int,"
          "type text,"
          "formula text);";

  std::string scalar_value_sql =
      "drop table if exists scalarValue;"
      "create table scalarValue ("
          "id int,"
          "dump int,"
          "value real,"
          "primary key (id, dump));";

  std::string vector_value_sql =
      "drop table if exists vectorValue;"
      "create table vectorValue ("
          "id int,"
          "dump int,"
          "value blob,"
          "primary key (id, dump));";

  std::string dist_value_sql =
      "drop table if exists distValue;"
      "create table distValue ("
          "id int,"
          "dump int,"
          "sum real,"
          "squares real,"
          "samples real,"
          "min real,"
          "max real,"
          "bucket_size real,"
          "vector blob,"
          "min_val real,"
          "max_val real,"
          "underflow real,"
          "overflow real,"
          "primary key (id, dump));";

  std::string dump_desc_sql =
      "drop table if exists dumpDesc;"
      "create table dumpDesc ("
          "id int primary key,"
          "desc text);";

  std::string all_sql;
  all_sql += stats_sql;
  all_sql += scalar_value_sql;
  all_sql += vector_value_sql;
  all_sql += dist_value_sql;
  all_sql += dump_desc_sql;

  char* errmsg;
  int ret = sqlite3_exec(db, all_sql.c_str(), nullptr, nullptr, &errmsg);
  if (ret == SQLITE_ABORT) {
    print_errmsg(ret, errmsg);
    sqlite3_free(errmsg);
    return false;
  } else {
    return true;
  }
}

void OutputSQL::write_metadata(const Info &info) {
  StatInfo metadata(info);
  exec_sql(metadata.create_sql_cmd(statName(info.name)));
}

void OutputSQL::begin(std::string desc) {
  if (exec_sql("begin deferred transaction;") != SQLITE_OK) {
    return;
  }
  std::string dump_desc_sql;
  dump_desc_sql += "insert into dumpDesc (id, desc) values (";
  dump_desc_sql += std::to_string(dump_count);
  dump_desc_sql += ", \"";
  dump_desc_sql += desc;
  dump_desc_sql += "\");";
  exec_sql(dump_desc_sql);
}

void OutputSQL::end() {
  exec_sql("commit transaction;");
  dump_count++;
}

std::string OutputSQL::statName(const std::string &name) const {
  if (path.empty())
    return name;
  else
    return csprintf("%s.%s", path.top(), name);
}

void OutputSQL::beginGroup(const char *name) {
    if (path.empty()) {
        path.push(name);
    } else {
        path.push(csprintf("%s.%s", path.top(), name));
    }
}

void OutputSQL::endGroup() {
    assert(!path.empty());
    path.pop();
}

int OutputSQL::insert_vector_value(int id, int dump, const unsigned char *blob,
                                   unsigned nbytes) {
  std::string sql =
      "insert into vectorValue (id, dump, value) values (?, ?, ?);";
  sqlite3_stmt *pstmt;
  int ret = sqlite3_prepare_v2(db, sql.c_str(), sql.size(), &pstmt, nullptr);
  if (ret != SQLITE_OK) {
    print_errmsg(ret);
    sqlite3_finalize(pstmt);
    return ret;
  }

  sqlite3_bind_int(pstmt, 1, id);
  sqlite3_bind_int(pstmt, 2, dump);
  sqlite3_bind_blob(pstmt, 3, blob, nbytes, SQLITE_TRANSIENT);

  ret = sqlite3_step(pstmt);
  if (ret != SQLITE_DONE) {
    print_errmsg(ret);
  }
  ret = sqlite3_finalize(pstmt);
  return ret;
}

void OutputSQL::visit(const ScalarInfo &info) {
  if (no_output(info))
    return;

  if (dump_count == 0)
    write_metadata(info);
  std::string sql = "insert into scalarValue (id, dump, value) values (?, ?, ?);";
  sqlite3_stmt *pstmt;
  int ret = sqlite3_prepare_v2(db, sql.c_str(), sql.size(), &pstmt, nullptr);
  if (ret != SQLITE_OK) {
    print_errmsg(ret);
    sqlite3_finalize(pstmt);
    return;
  }
  sqlite3_bind_int(pstmt, 1, info.id);
  sqlite3_bind_int(pstmt, 2, dump_count);
  sqlite3_bind_double(pstmt, 3, info.value());

  ret = sqlite3_step(pstmt);
  if (ret != SQLITE_DONE) {
    print_errmsg(ret);
  }
  sqlite3_finalize(pstmt);
}

void OutputSQL::visit(const VectorInfo &info) {
  if (no_output(info))
    return;

  if (dump_count == 0)
    write_metadata(info);
  // Store the vector of results as a simple blob - the backing C array itself.
  //
  const Result* vresult = info.result().data();
  unsigned nbytes = info.size() * sizeof(Result);
  unsigned char* blob = new unsigned char[nbytes];
  std::memcpy(blob, vresult, nbytes);
  insert_vector_value(info.id, dump_count, blob, nbytes);
  delete[] blob;
}

void OutputSQL::visit(const DistInfo &info) {
  if (no_output(info))
    return;

  if (dump_count == 0)
    write_metadata(info);
  const Counter* cvec = info.data.cvec.data();
  unsigned nbytes = info.data.cvec.size() * sizeof(Counter);
  unsigned char* blob = new unsigned char[nbytes];
  std::memcpy(blob, cvec, nbytes);

  std::string sql;
  if (info.data.type == Stats::DistType::Deviation) {
    sql = "insert into distValue (id, dump, sum, squares, samples) values (?, "
          "?, ?, ?, ?);";
  } else if (info.data.type == Stats::DistType::Dist) {
    sql = "insert into distValue (id, dump, sum, squares, samples, "
          "min, max, bucket_size, vector) values (?, ?, ?, ?, ?, "
          "?, ?, ?, ?); ";
  } else if (info.data.type == Stats::DistType::Hist) {
    sql = "insert into distValue (id, dump, sum, squares, samples, "
          "min, max, bucket_size, vector, min_val, max_val, "
          "underflow, overflow) values (?, ?, ?, ?, ?, ?, ?, ?, ?, "
          "?, ?, ?, ?); ";
  }

  sqlite3_stmt *pstmt;
  int ret = sqlite3_prepare_v2(db, sql.c_str(), sql.size(), &pstmt, nullptr);
  if (ret != SQLITE_OK) {
    print_errmsg(ret);
    sqlite3_finalize(pstmt);
    return;
  }

  sqlite3_bind_int(pstmt, 1, info.id);
  sqlite3_bind_int(pstmt, 2, dump_count);
  sqlite3_bind_double(pstmt, 3, info.data.sum);
  sqlite3_bind_double(pstmt, 4, info.data.squares);
  sqlite3_bind_double(pstmt, 5, info.data.samples);
  if (info.data.type == Stats::DistType::Dist ||
      info.data.type == Stats::DistType::Hist) {
    sqlite3_bind_double(pstmt, 6, info.data.min);
    sqlite3_bind_double(pstmt, 7, info.data.max);
    sqlite3_bind_double(pstmt, 8, info.data.bucket_size);
    sqlite3_bind_blob(pstmt, 9, blob, nbytes, SQLITE_TRANSIENT);
  }
  if (info.data.type == Stats::DistType::Hist) {
    sqlite3_bind_double(pstmt, 10, info.data.min_val);
    sqlite3_bind_double(pstmt, 11, info.data.max_val);
    sqlite3_bind_double(pstmt, 12, info.data.underflow);
    sqlite3_bind_double(pstmt, 13, info.data.overflow);
  }

  ret = sqlite3_step(pstmt);
  if (ret != SQLITE_DONE) {
    print_errmsg(ret);
  }
  sqlite3_finalize(pstmt);

  delete[] blob;
}

void OutputSQL::visit(const Vector2dInfo &info) {
  if (no_output(info))
    return;

  if (dump_count == 0)
    write_metadata(info);
  const Counter* cvec = info.cvec.data();
  unsigned nbytes = info.cvec.size() * sizeof(Counter);
  unsigned char* blob = new unsigned char[nbytes];
  std::memcpy(blob, cvec, nbytes);
  insert_vector_value(info.id, dump_count, blob, nbytes);
  delete[] blob;
}

void OutputSQL::visit(const FormulaInfo &info) {
  if (no_output(info))
    return;

  if (dump_count == 0)
    write_metadata(info);
  const Result* vresult = info.result().data();
  unsigned nbytes = info.size() * sizeof(Result);
  unsigned char* blob = new unsigned char[nbytes];
  std::memcpy(blob, vresult, nbytes);
  insert_vector_value(info.id, dump_count, blob, nbytes);
  delete[] blob;
}

void OutputSQL::visit(const VectorDistInfo &info) {}

void OutputSQL::visit(const SparseHistInfo &info) {}

bool OutputSQL::no_output(const Info& info) {
  if (!info.flags.isSet(display))
    return true;

  if (info.prereq && info.prereq->zero())
    return true;

  return false;
}

StatInfo::StatInfo(const Info &info, const std::string &_type,
                   const std::string &_formula, int _x, int _y)
    : id(info.id), name(info.name), desc(info.desc), precision(info.precision),
      flags(info.flags), type(_type), subnames(""), y_subnames(""),
      subdescs(""), x(_x), y(_y), formula(_formula) {
    if (info.prereq)
        prereq = info.prereq->id;
    else
        prereq = StatInfo::NO_PREREQ;
}

StatInfo::StatInfo(const ScalarInfo &info) : StatInfo(info, "ScalarInfo") {}

StatInfo::StatInfo(const VectorInfo &info) : StatInfo(info, "VectorInfo") {
    subnames = join(info.subnames);
    subdescs = join(info.subdescs);
}

StatInfo::StatInfo(const DistInfo &info) : StatInfo(info, "") {
    if (info.data.type == Stats::DistType::Deviation)
        type = "Deviation";
    else if (info.data.type == Stats::DistType::Dist)
        type = "Dist";
    else if (info.data.type == Stats::DistType::Hist)
        type = "Hist";
}

StatInfo::StatInfo(const Vector2dInfo &info)
    : StatInfo(info, "Vector2dInfo", "", info.x, info.y) {
  subnames = join(info.subnames);
  y_subnames = join(info.y_subnames);
  subdescs = join(info.subdescs);
}

StatInfo::StatInfo(const FormulaInfo &info)
    : StatInfo(info, "FormulaInfo", info.str()) {
    subnames = join(info.subnames);
    subdescs = join(info.subdescs);
}

StatInfo::StatInfo(const SparseHistInfo &info)
    : StatInfo(info, "SparseHistInfo") {}

StatInfo::StatInfo(const VectorDistInfo &info)
    : StatInfo(info, "VectorDistInfo") {}

std::string StatInfo::join(const std::vector<std::string> &array,
                           const std::string &sep) {
  std::string joined;
  for (const std::string &str : array) {
    joined += str;
    joined += sep;
  }
  return joined;
}

std::string StatInfo::create_sql_cmd(const std::string& statName) {
  std::stringstream sql_col_creator, sql_val_creator;
  sql_col_creator << "id, name, desc, flags, precision, type";
  sql_val_creator << id << ", \""
                  << statName << "\", \""
                  << desc << "\", "
                  << flags << ", "
                  << precision << ", \""
                  << type << "\"";
  if (prereq != StatInfo::NO_PREREQ) {
    sql_col_creator << ", prereq";
    sql_val_creator << ", " << prereq;
  }
  if (!subnames.empty()) {
    sql_col_creator << ", subnames";
    sql_val_creator << ", \"" << subnames << "\"";
  }
  if (!subdescs.empty()) {
    sql_col_creator << ", subdescs";
    sql_val_creator << ", \"" << subdescs << "\"";
  }
  if (!y_subnames.empty()) {
    sql_col_creator << ", y_subnames, x, y";
    sql_val_creator << ", \"" << y_subnames << "\", " << x << ", " << y;
  }

  std::string final_cmd = "insert into stats (" + sql_col_creator.str() +
                          ") values (" + sql_val_creator.str() + ");";
  return final_cmd;
}

Output *
initOutputSQL(const std::string &filename) {
  static OutputSQL sql;
  static bool connected = false;

  if (!connected) {
    sql.open(filename);
    connected = true;  // If it failed, it would have killed the sim.
  }

  return &sql;
}

}  // namespace Stats

#else

namespace Stats {

// SQLite3 libraries and headers were not found.

Output *
initOutputSQL(const std::string &filename) {
  return nullptr;
}

}  // namespace Stats

#endif  // ENABLE_SQLITE_STATS_OUTPUT
