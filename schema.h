#ifndef SCHEMA_H
#define SCHEMA_H
#include <string>
#include <vector>
#include <cstdint>

enum class ColType : uint8_t { INT = 0, TEXT = 1 };

struct Column {
    std::string name;
    ColType     type;
    uint16_t    max_len; // for TEXT: max bytes; for INT: always 4
};

struct Schema {
    std::string         table_name;
    std::vector<Column> cols;

    // Returns the column index for a given name, or -1 if not found.
    int find_col(const std::string& name) const;

    // Serialise/deserialise schema to a small byte buffer for persistence.
    // Format: 1-byte ncols, then for each col:
    //   1-byte type, 2-byte max_len, 1-byte name_len, name bytes
    std::vector<char> serialize() const;
    static Schema deserialize(const char* buf, uint32_t len);
};

// ── Row value ───────────────────────────────────────────────────────────────
// A single field value in a row.
struct Value {
    ColType type;
    int32_t int_val  = 0;
    std::string str_val;

    static Value make_int(int32_t v) { Value x; x.type=ColType::INT;  x.int_val=v; return x; }
    static Value make_text(const std::string& s) { Value x; x.type=ColType::TEXT; x.str_val=s; return x; }

    std::string to_string() const;
};

// A full row = ordered vector of Values matching the schema's column order.
using Row = std::vector<Value>;

// ── Row serialization ────────────────────────────────────────────────────────
// Encode/decode a Row to/from a compact byte buffer to store in a slotted page.
// Format per field:
//   INT:  4 bytes little-endian
//   TEXT: 2-byte length prefix + bytes (no null terminator)
std::vector<char> serialize_row(const Schema& schema, const Row& row);
Row               deserialize_row(const Schema& schema, const char* buf, uint16_t len);

#endif
