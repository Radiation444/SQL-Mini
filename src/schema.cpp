#include "schema.h"
#include <cstring>
#include <stdexcept>

int Schema::find_col(const std::string& name) const {
    for (int i = 0; i < (int)cols.size(); ++i)
        if (cols[i].name == name) return i;
    return -1;
}

std::vector<char> Schema::serialize() const {
    std::vector<char> out;
    uint8_t nc = (uint8_t)cols.size();
    out.push_back((char)nc);
    for (auto& c : cols) {
        out.push_back((char)c.type);
        out.push_back((char)(c.max_len & 0xFF));
        out.push_back((char)((c.max_len >> 8) & 0xFF));
        uint8_t nl = (uint8_t)c.name.size();
        out.push_back((char)nl);
        for (char ch : c.name) out.push_back(ch);
    }
    return out;
}

Schema Schema::deserialize(const char* buf, uint32_t len) {
    Schema s;
    if (len == 0) return s;
    uint32_t pos = 0;
    uint8_t nc = (uint8_t)buf[pos++];
    for (uint8_t i = 0; i < nc && pos < len; ++i) {
        Column c;
        c.type    = static_cast<ColType>((uint8_t)buf[pos++]);
        uint16_t ml = (uint8_t)buf[pos] | ((uint8_t)buf[pos+1] << 8); pos += 2;
        c.max_len = ml;
        uint8_t nl = (uint8_t)buf[pos++];
        c.name.assign(buf + pos, nl); pos += nl;
        s.cols.push_back(c);
    }
    return s;
}

std::string Value::to_string() const {
    if (type == ColType::INT) return std::to_string(int_val);
    return str_val;
}

// ── Row serialization ────────────────────────────────────────────────────────

std::vector<char> serialize_row(const Schema& schema, const Row& row) {
    std::vector<char> buf;
    for (size_t i = 0; i < schema.cols.size() && i < row.size(); ++i) {
        const Value& v = row[i];
        if (schema.cols[i].type == ColType::INT) {
            int32_t n = v.int_val;
            char tmp[4];
            std::memcpy(tmp, &n, 4);
            buf.insert(buf.end(), tmp, tmp + 4);
        } else {
            const std::string& s = v.str_val;
            uint16_t slen = (uint16_t)s.size();
            buf.push_back((char)(slen & 0xFF));
            buf.push_back((char)(slen >> 8));
            buf.insert(buf.end(), s.begin(), s.end());
        }
    }
    return buf;
}

Row deserialize_row(const Schema& schema, const char* buf, uint16_t len) {
    Row row;
    uint16_t pos = 0;
    for (auto& col : schema.cols) {
        if (pos >= len) break;
        Value v;
        v.type = col.type;
        if (col.type == ColType::INT) {
            int32_t n = 0;
            std::memcpy(&n, buf + pos, 4); pos += 4;
            v.int_val = n;
        } else {
            uint16_t slen = (uint8_t)buf[pos] | ((uint8_t)buf[pos+1] << 8); pos += 2;
            v.str_val.assign(buf + pos, slen); pos += slen;
        }
        row.push_back(v);
    }
    return row;
}
