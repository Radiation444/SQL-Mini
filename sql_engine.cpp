#include "sql_engine.h"
#include <sstream>
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <cstring>

// ── utilities ────────────────────────────────────────────────────────────────

std::string SQLEngine::trim(const std::string& s) {
    size_t a = s.find_first_not_of(" \t\r\n");
    if (a == std::string::npos) return "";
    size_t b = s.find_last_not_of(" \t\r\n");
    return s.substr(a, b - a + 1);
}

static std::string to_upper(std::string s) {
    for (char& c : s) c = (char)std::toupper((unsigned char)c);
    return s;
}

// Very small tokenizer: splits on whitespace and punctuation (, ( ) ; = < > !)
// and preserves quoted strings as single tokens (without the quotes).
std::vector<std::string> SQLEngine::tokenize(const std::string& sql) {
    std::vector<std::string> toks;
    size_t i = 0, n = sql.size();
    while (i < n) {
        char c = sql[i];
        if (std::isspace((unsigned char)c)) { ++i; continue; }
        if (c == '\'' || c == '"') {
            char q = c; ++i;
            std::string s;
            while (i < n && sql[i] != q) s += sql[i++];
            if (i < n) ++i; // consume closing quote
            toks.push_back(s);
            continue;
        }
        // Two-char operators
        if (i + 1 < n) {
            std::string two = sql.substr(i, 2);
            if (two == "<=" || two == ">=" || two == "!=" || two == "<>") {
                toks.push_back(two == "<>" ? "!=" : two);
                i += 2; continue;
            }
        }
        if (c == ',' || c == '(' || c == ')' || c == ';' ||
            c == '=' || c == '<' || c == '>' || c == '*') {
            toks.push_back(std::string(1, c));
            ++i; continue;
        }
        // Identifier / number / keyword
        std::string tok;
        while (i < n && !std::isspace((unsigned char)sql[i]) &&
               sql[i] != ',' && sql[i] != '(' && sql[i] != ')' &&
               sql[i] != ';' && sql[i] != '\'' && sql[i] != '"') {
            tok += sql[i++];
        }
        if (!tok.empty()) toks.push_back(tok);
    }
    return toks;
}

// ── SQLEngine lifecycle ──────────────────────────────────────────────────────

SQLEngine::SQLEngine() {}

SQLEngine::~SQLEngine() {
    for (auto& [name, h] : tables_) {
        delete h.table;
        delete h.pool;
        delete h.pager;
    }
}

SQLEngine::TableHandle& SQLEngine::open_table(const std::string& name,
                                               const Schema* schema_if_create) {
    auto it = tables_.find(name);
    if (it != tables_.end()) return it->second;

    std::string fname = name + ".db";
    Pager*      pager = new Pager(fname);
    BufferPool* pool  = new BufferPool(pager, 256);
    bool        is_new = (pager->get_num_pages() == 0);

    Schema schema;
    schema.table_name = name;
    if (is_new) {
        if (!schema_if_create)
            throw std::runtime_error("Table '" + name + "' does not exist. Use CREATE TABLE first.");
        schema = *schema_if_create;
    }
    // For existing tables, schema is read from disk inside the Table constructor.

    Table* table = new Table(pool, schema, is_new);
    tables_[name] = {pager, pool, table};
    return tables_[name];
}

// ── CREATE TABLE ─────────────────────────────────────────────────────────────
// CREATE TABLE name ( col1 TYPE, col2 TYPE(n), ... )

std::string SQLEngine::exec_create(const std::string& sql) {
    auto toks = tokenize(sql);
    // Expected: CREATE TABLE name ( col type [, col type]* )
    size_t i = 0;
    auto expect = [&](const std::string& s) {
        if (i >= toks.size() || to_upper(toks[i]) != s)
            throw std::runtime_error("Syntax error near '" + (i < toks.size() ? toks[i] : "EOF") + "', expected '" + s + "'");
        ++i;
    };
    expect("CREATE"); expect("TABLE");
    if (i >= toks.size()) throw std::runtime_error("Expected table name");
    std::string tname = toks[i++];
    expect("(");

    Schema schema;
    schema.table_name = tname;

    while (i < toks.size() && toks[i] != ")") {
        std::string cname = toks[i++];
        if (i >= toks.size()) throw std::runtime_error("Expected column type");
        std::string ctype = to_upper(toks[i++]);
        Column col;
        col.name = cname;
        if (ctype == "INT" || ctype == "INTEGER") {
            col.type    = ColType::INT;
            col.max_len = 4;
        } else if (ctype == "TEXT" || ctype == "VARCHAR") {
            col.type    = ColType::TEXT;
            col.max_len = 255;
            // Optional (n)
            if (i < toks.size() && toks[i] == "(") {
                ++i; // skip (
                if (i < toks.size() && toks[i] != ")") {
                    col.max_len = (uint16_t)std::stoi(toks[i++]);
                }
                if (i < toks.size() && toks[i] == ")") ++i;
            }
        } else {
            throw std::runtime_error("Unknown type: " + ctype);
        }
        schema.cols.push_back(col);
        if (i < toks.size() && toks[i] == ",") ++i;
    }

    open_table(tname, &schema);
    return "Table '" + tname + "' created with " + std::to_string(schema.cols.size()) + " column(s).";
}

// ── INSERT INTO ──────────────────────────────────────────────────────────────
// INSERT INTO name [(col,...)] VALUES (v,...)

std::string SQLEngine::exec_insert(const std::string& sql) {
    auto toks = tokenize(sql);
    size_t i = 0;
    auto expect = [&](const std::string& s) {
        if (i >= toks.size() || to_upper(toks[i]) != s)
            throw std::runtime_error("Syntax error near '" + (i < toks.size() ? toks[i] : "EOF") + "'");
        ++i;
    };
    expect("INSERT"); expect("INTO");
    std::string tname = toks[i++];

    auto& h = open_table(tname);
    const Schema& schema = h.table->schema();

    // Optional column list
    std::vector<std::string> col_order;
    if (i < toks.size() && toks[i] == "(") {
        ++i;
        while (i < toks.size() && toks[i] != ")") {
            col_order.push_back(toks[i++]);
            if (i < toks.size() && toks[i] == ",") ++i;
        }
        if (i < toks.size()) ++i; // skip )
    }

    expect("VALUES"); expect("(");

    std::vector<std::string> vals;
    while (i < toks.size() && toks[i] != ")") {
        vals.push_back(toks[i++]);
        if (i < toks.size() && toks[i] == ",") ++i;
    }

    // Build row in schema column order
    Row row(schema.cols.size());
    if (col_order.empty()) {
        // Positional
        for (size_t j = 0; j < schema.cols.size() && j < vals.size(); ++j) {
            if (schema.cols[j].type == ColType::INT) {
                row[j] = Value::make_int(std::stoi(vals[j]));
            } else {
                row[j] = Value::make_text(vals[j]);
            }
        }
    } else {
        for (size_t j = 0; j < col_order.size() && j < vals.size(); ++j) {
            int ci = schema.find_col(col_order[j]);
            if (ci < 0) throw std::runtime_error("Unknown column: " + col_order[j]);
            if (schema.cols[ci].type == ColType::INT) {
                row[ci] = Value::make_int(std::stoi(vals[j]));
            } else {
                row[ci] = Value::make_text(vals[j]);
            }
        }
    }

    uint32_t rowid = h.table->insert_row(row);
    return "Inserted 1 row (rowid=" + std::to_string(rowid) + ").";
}

// ── SELECT ───────────────────────────────────────────────────────────────────
// SELECT * | col,... FROM name [WHERE col op val] [LIMIT n]

bool SQLEngine::match_where(const Row& row, const Schema& schema, const WhereClause& wh) {
    if (!wh.active) return true;
    int ci = schema.find_col(wh.col);
    if (ci < 0 || ci >= (int)row.size()) return false;
    const Value& v = row[ci];
    if (schema.cols[ci].type == ColType::INT) {
        int32_t rv = v.int_val;
        int32_t cv = std::stoi(wh.val);
        if (wh.op == "=")  return rv == cv;
        if (wh.op == "!=") return rv != cv;
        if (wh.op == "<")  return rv <  cv;
        if (wh.op == ">")  return rv >  cv;
        if (wh.op == "<=") return rv <= cv;
        if (wh.op == ">=") return rv >= cv;
    } else {
        const std::string& rs = v.str_val;
        const std::string& cs = wh.val;
        if (wh.op == "=")  return rs == cs;
        if (wh.op == "!=") return rs != cs;
        if (wh.op == "<")  return rs <  cs;
        if (wh.op == ">")  return rs >  cs;
        if (wh.op == "<=") return rs <= cs;
        if (wh.op == ">=") return rs >= cs;
    }
    return false;
}

SelectResult SQLEngine::exec_select(const std::string& sql) {
    SelectResult res;
    auto toks = tokenize(sql);
    size_t i = 0;
    auto expect = [&](const std::string& s) {
        if (i >= toks.size() || to_upper(toks[i]) != s)
            throw std::runtime_error("Syntax error near '" + (i < toks.size() ? toks[i] : "EOF") + "'");
        ++i;
    };
    expect("SELECT");

    bool star = false;
    std::vector<std::string> sel_cols;
    if (i < toks.size() && toks[i] == "*") { star = true; ++i; }
    else {
        while (i < toks.size() && to_upper(toks[i]) != "FROM") {
            sel_cols.push_back(toks[i++]);
            if (i < toks.size() && toks[i] == ",") ++i;
        }
    }
    expect("FROM");
    std::string tname = toks[i++];

    auto& h = open_table(tname);
    const Schema& schema = h.table->schema();

    // WHERE
    WhereClause wh;
    if (i < toks.size() && to_upper(toks[i]) == "WHERE") {
        ++i;
        wh.active = true;
        wh.col = toks[i++];
        wh.op  = toks[i++];
        wh.val = toks[i++];
    }

    // LIMIT
    int32_t limit = -1;
    if (i < toks.size() && to_upper(toks[i]) == "LIMIT") {
        ++i;
        limit = std::stoi(toks[i++]);
    }

    // Build output column indices
    std::vector<int> out_idx;
    if (star) {
        for (int j = 0; j < (int)schema.cols.size(); ++j) out_idx.push_back(j);
        for (auto& c : schema.cols) res.cols.push_back(c.name);
    } else {
        for (auto& cn : sel_cols) {
            int ci = schema.find_col(cn);
            if (ci < 0) { res.error = "Unknown column: " + cn; return res; }
            out_idx.push_back(ci);
            res.cols.push_back(cn);
        }
    }

    int32_t returned = 0;

    // Fast path: WHERE col0 = <int>  →  single BTree point lookup (O(log N))
    bool used_fast_path = false;
    if (wh.active && wh.op == "=" && !schema.cols.empty() &&
        schema.cols[0].type == ColType::INT &&
        schema.find_col(wh.col) == 0) {
        uint32_t target_rowid = (uint32_t)std::stoi(wh.val);
        Row row;
        if (h.table->find_by_rowid(target_rowid, row)) {
            ++res.rows_scanned;
            std::vector<std::string> out_row;
            for (int ci : out_idx)
                out_row.push_back(ci < (int)row.size() ? row[ci].to_string() : "NULL");
            res.rows.push_back(std::move(out_row));
            ++returned;
        }
        used_fast_path = true;
    }

    if (!used_fast_path) {
        h.table->scan([&](uint32_t /*rowid*/, const Row& row) -> bool {
            ++res.rows_scanned;
            if (!match_where(row, schema, wh)) return true;
            std::vector<std::string> out_row;
            for (int ci : out_idx) {
                out_row.push_back(ci < (int)row.size() ? row[ci].to_string() : "NULL");
            }
            res.rows.push_back(std::move(out_row));
            ++returned;
            if (limit > 0 && returned >= limit) return false;
            return true;
        });
    }
    return res;
}

// ── DELETE FROM ──────────────────────────────────────────────────────────────
// DELETE FROM name [WHERE col op val]

std::string SQLEngine::exec_delete(const std::string& sql) {
    auto toks = tokenize(sql);
    size_t i = 0;
    auto expect = [&](const std::string& s) {
        if (i >= toks.size() || to_upper(toks[i]) != s)
            throw std::runtime_error("Syntax error near '" +
                (i < toks.size() ? toks[i] : "EOF") + "', expected '" + s + "'");
        ++i;
    };
    expect("DELETE"); expect("FROM");
    if (i >= toks.size()) throw std::runtime_error("Expected table name");
    std::string tname = toks[i++];

    auto& h = open_table(tname);
    const Schema& schema = h.table->schema();

    // Optional WHERE clause (reuses WhereClause + match_where).
    WhereClause wh;
    if (i < toks.size() && to_upper(toks[i]) == "WHERE") {
        ++i;
        wh.active = true;
        wh.col = toks[i++];
        wh.op  = toks[i++];
        wh.val = toks[i++];
    }

    uint32_t deleted = h.table->delete_where(
        [&](uint32_t /*rowid*/, const Row& row) -> bool {
            return match_where(row, schema, wh);
        });

    return "Deleted " + std::to_string(deleted) + " row(s).";
}

// ── JOIN helpers ─────────────────────────────────────────────────────────────

// Resolve a column reference (bare "col" or "table.col") against the flat
// combined_cols list produced by the join executor ("table.col" strings).
// Returns the index into the combined row, or -1 if not found / ambiguous.
int SQLEngine::resolve_col(const std::string& ref,
                           const std::vector<std::string>& combined_cols) {
    // Qualified reference: find exact match.
    if (ref.find('.') != std::string::npos) {
        for (int i = 0; i < (int)combined_cols.size(); ++i)
            if (combined_cols[i] == ref) return i;
        return -1;
    }
    // Bare name: find unambiguous match.
    int found = -1;
    for (int i = 0; i < (int)combined_cols.size(); ++i) {
        auto dot = combined_cols[i].find('.');
        std::string bare = (dot == std::string::npos)
                           ? combined_cols[i]
                           : combined_cols[i].substr(dot + 1);
        if (bare == ref) {
            if (found != -1) return -2; // ambiguous
            found = i;
        }
    }
    return found;
}

// WHERE match against a string-valued combined row (post-join).
// Numeric comparison is attempted first; falls back to string comparison.
bool SQLEngine::match_where_combined(const std::vector<std::string>& row,
                                     const std::vector<std::string>& combined_cols,
                                     const WhereClause& wh) {
    if (!wh.active) return true;
    int ci = resolve_col(wh.col, combined_cols);
    if (ci < 0 || ci >= (int)row.size()) return false;
    const std::string& rv = row[ci];
    const std::string& cv = wh.val;
    // Try numeric comparison.
    try {
        int32_t ri = std::stoi(rv), ci2 = std::stoi(cv);
        if (wh.op == "=")  return ri == ci2;
        if (wh.op == "!=") return ri != ci2;
        if (wh.op == "<")  return ri <  ci2;
        if (wh.op == ">")  return ri >  ci2;
        if (wh.op == "<=") return ri <= ci2;
        if (wh.op == ">=") return ri >= ci2;
    } catch (...) {}
    // String comparison.
    if (wh.op == "=")  return rv == cv;
    if (wh.op == "!=") return rv != cv;
    if (wh.op == "<")  return rv <  cv;
    if (wh.op == ">")  return rv >  cv;
    if (wh.op == "<=") return rv <= cv;
    if (wh.op == ">=") return rv >= cv;
    return false;
}

// ── SELECT ... JOIN ───────────────────────────────────────────────────────────
// SELECT */col,... FROM t1 [INNER] JOIN t2 ON t1.col = t2.col
//        [WHERE col op val] [LIMIT n]
//
// Strategy: nested-loop join.
//   1. Load all rows from the right (inner) table into memory.
//   2. For each left row, iterate the right table looking for ON matches.
//   3. Apply optional WHERE filter and LIMIT on the combined row.

SelectResult SQLEngine::exec_join(const std::string& sql) {
    SelectResult res;
    auto toks = tokenize(sql);
    size_t i = 0;

    auto expect = [&](const std::string& s) {
        if (i >= toks.size() || to_upper(toks[i]) != s)
            throw std::runtime_error("Syntax error near '" +
                (i < toks.size() ? toks[i] : "EOF") + "', expected '" + s + "'");
        ++i;
    };
    auto peek = [&]() -> std::string {
        return (i < toks.size()) ? to_upper(toks[i]) : "";
    };

    expect("SELECT");

    // Collect SELECT list (raw tokens, resolved after we know schemas).
    bool star = false;
    std::vector<std::string> sel_refs; // "table.col" or bare "col" or "*"
    if (peek() == "*") { star = true; ++i; }
    else {
        while (i < toks.size() && peek() != "FROM") {
            sel_refs.push_back(toks[i++]);
            if (peek() == ",") ++i;
        }
    }

    expect("FROM");
    std::string left_name = toks[i++];

    // Optional INNER keyword.
    if (peek() == "INNER") ++i;
    expect("JOIN");
    std::string right_name = toks[i++];

    expect("ON");

    // Parse ON: left_ref = right_ref (either order; we normalise below).
    std::string on_left_raw  = toks[i++];
    expect("=");
    std::string on_right_raw = toks[i++];

    // Helper to split "table.col" → {table, col}; bare "col" → {"", col}.
    auto split_ref = [](const std::string& ref) -> std::pair<std::string,std::string> {
        auto dot = ref.find('.');
        if (dot == std::string::npos) return {"", ref};
        return {ref.substr(0, dot), ref.substr(dot + 1)};
    };

    auto [on_lt, on_lc] = split_ref(on_left_raw);
    auto [on_rt, on_rc] = split_ref(on_right_raw);

    // Optional WHERE.
    WhereClause wh;
    if (i < toks.size() && peek() == "WHERE") {
        ++i;
        wh.active = true;
        wh.col = toks[i++];
        wh.op  = toks[i++];
        wh.val = toks[i++];
    }

    // Optional LIMIT.
    int32_t limit = -1;
    if (i < toks.size() && peek() == "LIMIT") {
        ++i;
        limit = std::stoi(toks[i++]);
    }

    // Open both tables.
    auto& lh = open_table(left_name);
    auto& rh = open_table(right_name);
    const Schema& ls = lh.table->schema();
    const Schema& rs = rh.table->schema();

    // Build the flat combined column name list: "table.col" for every column.
    std::vector<std::string> combined_cols;
    for (auto& c : ls.cols) combined_cols.push_back(left_name  + "." + c.name);
    for (auto& c : rs.cols) combined_cols.push_back(right_name + "." + c.name);

    int left_ncols  = (int)ls.cols.size();

    // Normalise the ON clause so on_left always refers to the left table.
    // If the user wrote right.col = left.col, swap them.
    if (!on_lt.empty() && on_lt == right_name) {
        std::swap(on_lt, on_rt);
        std::swap(on_lc, on_rc);
    }
    // Resolve ON column indices in their respective schemas.
    int on_li = ls.find_col(on_lc);
    int on_ri = rs.find_col(on_rc);
    if (on_li < 0) { res.error = "ON: unknown column '" + on_lc + "' in " + left_name;  return res; }
    if (on_ri < 0) { res.error = "ON: unknown column '" + on_rc + "' in " + right_name; return res; }

    // Build output column indices into the combined row.
    std::vector<int> out_idx;
    if (star) {
        for (int j = 0; j < (int)combined_cols.size(); ++j) {
            out_idx.push_back(j);
            res.cols.push_back(combined_cols[j]);
        }
    } else {
        for (auto& ref : sel_refs) {
            int ci = resolve_col(ref, combined_cols);
            if (ci == -2) { res.error = "Ambiguous column '" + ref + "': qualify with table name"; return res; }
            if (ci  < 0)  { res.error = "Unknown column '" + ref + "'"; return res; }
            out_idx.push_back(ci);
            res.cols.push_back(combined_cols[ci]);
        }
    }

    // Materialise the right (inner) table into memory.
    // Each entry is a string-valued combined row with left_ncols blanks prepended
    // so indices align with combined_cols. We store only the right portion.
    struct RightRow { std::string join_key; std::vector<std::string> vals; };
    std::vector<RightRow> right_rows;
    rh.table->scan([&](uint32_t, const Row& row) -> bool {
        RightRow rr;
        rr.join_key = (on_ri < (int)row.size()) ? row[on_ri].to_string() : "";
        rr.vals.reserve(row.size());
        for (auto& v : row) rr.vals.push_back(v.to_string());
        right_rows.push_back(std::move(rr));
        return true;
    });

    // Nested-loop join.
    int32_t returned = 0;
    lh.table->scan([&](uint32_t, const Row& lrow) -> bool {
        std::string lkey = (on_li < (int)lrow.size()) ? lrow[on_li].to_string() : "";

        for (auto& rr : right_rows) {
            ++res.rows_scanned;
            if (lkey != rr.join_key) continue; // ON predicate

            // Build the combined string row.
            std::vector<std::string> combined;
            combined.reserve(left_ncols + (int)rr.vals.size());
            for (auto& v : lrow) combined.push_back(v.to_string());
            combined.insert(combined.end(), rr.vals.begin(), rr.vals.end());

            // Apply WHERE.
            if (!match_where_combined(combined, combined_cols, wh)) continue;

            // Project selected columns.
            std::vector<std::string> out_row;
            out_row.reserve(out_idx.size());
            for (int ci : out_idx)
                out_row.push_back(ci < (int)combined.size() ? combined[ci] : "NULL");
            res.rows.push_back(std::move(out_row));
            ++returned;
            if (limit > 0 && returned >= limit) return false;
        }
        return true;
    });

    return res;
}

// ── Public exec ──────────────────────────────────────────────────────────────

std::string SQLEngine::exec(const std::string& raw_sql) {
    std::string sql = trim(raw_sql);
    // Strip trailing semicolon
    if (!sql.empty() && sql.back() == ';') sql.pop_back();
    if (sql.empty()) return "";

    std::string keyword = to_upper(sql.substr(0, 6));
    try {
        if (keyword == "CREATE") return exec_create(sql);
        if (keyword == "INSERT") return exec_insert(sql);
        if (keyword == "DELETE") return exec_delete(sql);
        if (keyword == "SELECT") {
            // Peek for JOIN keyword to route to the join executor.
            auto toks_peek = tokenize(sql);
            bool has_join = false;
            for (auto& t : toks_peek)
                if (to_upper(t) == "JOIN") { has_join = true; break; }

            SelectResult res = has_join ? exec_join(sql) : exec_select(sql);
            if (!res.error.empty()) return "Error: " + res.error;
            // Pretty-print table
            std::ostringstream out;
            // Header
            for (size_t j = 0; j < res.cols.size(); ++j) {
                if (j) out << " | ";
                out << res.cols[j];
            }
            out << "\n";
            for (size_t j = 0; j < res.cols.size(); ++j) {
                if (j) out << "-+-";
                out << std::string(res.cols[j].size(), '-');
            }
            out << "\n";
            for (auto& row : res.rows) {
                for (size_t j = 0; j < row.size(); ++j) {
                    if (j) out << " | ";
                    out << row[j];
                }
                out << "\n";
            }
            out << "(" << res.rows.size() << " row(s), scanned " << res.rows_scanned << ")\n";
            return out.str();
        }
        return "Error: Unknown statement. Supported: CREATE TABLE, INSERT INTO, SELECT, DELETE FROM.";
    } catch (const std::exception& e) {
        return std::string("Error: ") + e.what();
    }
}

SelectResult SQLEngine::select(const std::string& sql) {
    try { return exec_select(sql); }
    catch (const std::exception& e) {
        SelectResult r; r.error = e.what(); return r;
    }
}