#ifndef TABLE_H
#define TABLE_H
#include "schema.h"
#include "btree.h"
#include "buffer_pool.h"
#include "slotted_page.h"
#include <functional>
#include <string>

const uint32_t SCHEMA_PAGE_ID = 1023;
const uint32_t HEAP_PAGE_BASE = 512;

inline uint32_t pack_ptr(uint32_t page_id, uint16_t slot) {
    return ((page_id - HEAP_PAGE_BASE) << 12) | (slot & 0xFFF);
}
inline uint32_t ptr_page(uint32_t packed) { return (packed >> 12) + HEAP_PAGE_BASE; }
inline uint16_t ptr_slot(uint32_t packed)  { return packed & 0xFFF; }

class Table {
public:
    // create=true  → initialise a fresh table and write schema to disk.
    // create=false → read schema + meta from disk (existing table).
    Table(BufferPool* pool, const Schema& schema, bool create);
    ~Table(); // flushes meta + buffer pool before BTree is destroyed

    uint32_t insert_row(const Row& row);
    uint32_t scan(const std::function<bool(uint32_t, const Row&)>& cb) const;
    bool     find_by_rowid(uint32_t rowid, Row& out) const;
    bool     delete_row(uint32_t rowid);   // delete a single row by rowid
    uint32_t delete_where(const std::function<bool(uint32_t, const Row&)>& pred); // returns count

    const Schema& schema() const { return schema_; }
    uint32_t      next_rowid() const { return next_rowid_; }

private:
    BufferPool* pool_;
    Schema      schema_;
    BTree*      btree_;
    uint32_t    next_rowid_;
    uint32_t    cur_heap_page_;

    char* heap_page(uint32_t page_id);
};
#endif