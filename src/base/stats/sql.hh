/*
 * Output gem5 stats to a SQLite3 database.
 *
 * This supports the same statistic types as the current Stats::Text output
 * module, except for VectorDistInfo and SparseHistInfo (these have more
 * complex data structures which will require more specialized serialization).
 *
 * This replaces the earlier OutputSQL implementation, which was written in
 * Python and required SWIG's cross language polymorphism features. This new
 * implementation is entirely written in C++ and fully compatible with
 * PyBind11. It was written to be as similar to the original Python
 * implementation as possible.
 *
 * Statistic types with vector-based storage (e.g. VectorInfo, Dist, etc.) have
 * vector data serialized into a packed array of doubles. This can be retrieved
 * and directly unpacked into a C array. For example, in Python 2.7:
 *
 *   struct.unpack("d", results[0:8])
 *
 * where results is the read/write buffer object obtained from a select on
 * the value column. This would get the first double out of the total packed
 * vector (and the buffer will indicate the total size in bytes).
 *
 * Author: Sam Xi.
 */

#ifndef __BASE_STATS_SQL_HH__
#define __BASE_STATS_SQL_HH__

#include <iosfwd>
#include <stack>
#include <string>

#include "base/stats/info.hh"
#include "base/stats/output.hh"

#ifdef ENABLE_SQLITE_STATS_OUTPUT

#include <sqlite3.h>

namespace Stats {

class OutputSQL: public Output {
  public:
    // Constructor that does not create a database.
    OutputSQL();
    // Constructor that also creates a database by calling open().
    OutputSQL(const std::string &filename);
    // Closes the database connection.
    virtual ~OutputSQL();

    // Creates a new SQLite3 database with the given filename and all tables.
    //
    // If a database already exists at the location, it is overwritten.
    void open(const std::string &filename);

    std::string statName(const std::string &name) const;

    // Statistic object visitors.
    virtual void visit(const ScalarInfo &info) override;
    virtual void visit(const VectorInfo &info) override;
    virtual void visit(const DistInfo &info) override;
    virtual void visit(const Vector2dInfo &info) override;
    virtual void visit(const FormulaInfo &info) override;

    // TODO: Not supported for now.
    virtual void visit(const VectorDistInfo &info) override;
    virtual void visit(const SparseHistInfo &info) override;

    virtual bool valid() const override;
    virtual void begin(std::string desc="") override;
    virtual void end() override;

    void beginGroup(const char *name) override;
    void endGroup() override;

  protected:
    // Creates all the tables used to store statistics info and values.
    bool create_tables();

    // Write metadata of the stats into the table named "stats".
    void write_metadata(const Info &info);

    // Executes a SQL command and returns the return code.
    //
    // This is just a wrapper for sqlite3_exec().
    int exec_sql(const std::string& sql_cmd);

    // Inserts a row of vector blob data into the vector stat table.
    int insert_vector_value(int id, int dump_count, const unsigned char *blob,
                            unsigned nbytes);

    // Returns true if this stat should not be output.
    bool no_output(const Info &info);

    // Print an error message based on the given error code.
    //
    // If errmsg is not NULL, then it is used; otherwise, we ask SQLite3 for
    // the error message.
    void print_errmsg(int code, char* errmsg = nullptr);

    // The SQLite3 database object.
    sqlite3* db;

    // True if the tables have been created successfully.
    bool tables_created;

    // How many times the stats have been dumped.
    //
    // This gets recorded along with each stat value so that stats for distinct
    // epochs of simulation can be distinguished.
    int dump_count;

    // Object/group path
    std::stack<std::string> path;
};

class StatInfo {
  public:
    StatInfo(const Info &info, const std::string &_type = "",
             const std::string &_formula = "", int _x = 0, int _y = 0);
    StatInfo(const ScalarInfo& info);
    StatInfo(const VectorInfo& info);
    StatInfo(const DistInfo& info);
    StatInfo(const Vector2dInfo& info);
    StatInfo(const FormulaInfo& info);

    // TODO: Not supported for now.
    StatInfo(const SparseHistInfo& info);
    StatInfo(const VectorDistInfo& info);

    std::string create_sql_cmd(const std::string& statName);

  protected:
    static const int NO_PREREQ = -1;

    std::string join(const std::vector<std::string> &array,
                     const std::string &sep = ",");

    int id;
    std::string name;
    std::string desc;
    int precision;
    int flags;
    int prereq;
    // Stringified type of the stat (ScalarInfo, VectorInfo, etc.)
    std::string type;

    std::string subnames;
    std::string y_subnames;
    std::string subdescs;
    int x;
    int y;
    std::string formula;
};

} // namespace Stats

#endif  // ENABLE_SQLITE_STATS_OUTPUT

namespace Stats {

Output *initOutputSQL(const std::string &filename);

}

#endif // __BASE_STATS_SQL_HH__
