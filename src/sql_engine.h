#ifndef SQL_ENGINE_H
#define SQL_ENGINE_H
#include "table.h"
#include "buffer_pool.h"
#include "pager.h"
#include <string>
#include <vector>
#include <memory>
#include <unordered_map>

/*
 * Tiny SQL Engine
 * ───────────────
 * Supported statements:
 *
 *   CREATE TABLE name (col1 INT, col2 TEXT(n), ...);
 *
 *   INSERT INTO name (col1, col2, ...) VALUES (v1, v2, ...);
 *   INSERT INTO name VALUES (v1, v2, ...);   -- positional
 *
 *   SELECT col1, col2 FROM name [WHERE col op val] [LIMIT n];
 *   SELECT * FROM name [WHERE col op val] [LIMIT n];
 *
 *   SELECT t1.col, t2.col FROM t1 JOIN t2 ON t1.col = t2.col
 *          [WHERE t1.col op val] [LIMIT n];
 *   SELECT * FROM t1 JOIN t2 ON t1.col = t2.col;
 *
 * WHERE supports: =  !=  <  >  <=  >=  (single predicate)
 * JOIN   supports: INNER JOIN only; exactly one ON equality condition.
 *                  Column refs may be bare (unambiguous) or table.column.
 *
 * Each table has its own db file: "<name>.db"
 */

struct WhereClause {
    bool        active = false;
    std::string col;   // bare name or "table.col"
    std::string op;    // "=","!=","<",">","<=",">="
    std::string val;   // raw string from SQL; compared after type coercion
};

// Parsed ON clause for a JOIN: left_table.left_col = right_table.right_col
struct JoinClause {
    std::string left_table;
    std::string left_col;
    std::string right_table;
    std::string right_col;
};

struct SelectResult {
    std::vector<std::string>              cols;   // selected column names (* = all)
    std::vector<std::vector<std::string>> rows;   // string-ified values
    std::string error;
    uint32_t    rows_scanned = 0;
};

class SQLEngine {
public:
    SQLEngine();
    ~SQLEngine();

    // Execute one SQL statement. Returns a human-readable result string.
    std::string exec(const std::string& sql);

    // Lower-level: returns structured result for SELECT.
    SelectResult select(const std::string& sql);

private:
    // Loaded tables: name → (Pager*, BufferPool*, Table*)
    struct TableHandle {
        Pager*      pager;
        BufferPool* pool;
        Table*      table;
    };
    std::unordered_map<std::string, TableHandle> tables_;

    TableHandle& open_table(const std::string& name, const Schema* schema_if_create = nullptr);

    std::string exec_create(const std::string& sql);
    std::string exec_insert(const std::string& sql);
    std::string exec_delete(const std::string& sql);
    SelectResult exec_select(const std::string& sql);
    SelectResult exec_join(const std::string& sql);

    // Resolve "table.col" or bare "col" against a combined schema.
    // combined_cols is a flat list of "table.col" strings in row order.
    static int resolve_col(const std::string& ref,
                           const std::vector<std::string>& combined_cols);

    static std::string trim(const std::string& s);
    static std::vector<std::string> tokenize(const std::string& sql);
    static bool match_where(const Row& row, const Schema& schema, const WhereClause& wh);
    // WHERE match against a combined (joined) row using combined_cols for name resolution.
    static bool match_where_combined(const std::vector<std::string>& row,
                                     const std::vector<std::string>& combined_cols,
                                     const WhereClause& wh);
};

#endif