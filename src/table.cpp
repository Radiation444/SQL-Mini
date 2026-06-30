#include "table.h"
#include <cstring>
#include <stdexcept>
#include <limits>
#include <vector>

// ── Schema persistence ───────────────────────────────────────────────────────
// Page SCHEMA_PAGE_ID layout:
//   [0-3]  next_rowid (uint32_t)
//   [4-7]  cur_heap_page (uint32_t)
//   [8-11] schema_bytes length (uint32_t)
//   [12..] serialised schema

static void write_meta(BufferPool* pool, const Schema& schema,
                       uint32_t next_rowid, uint32_t cur_heap_page) {
    char* buf = pool->get_page(SCHEMA_PAGE_ID);
    std::memcpy(buf,     &next_rowid,     4);
    std::memcpy(buf + 4, &cur_heap_page,  4);
    auto sb = schema.serialize();
    uint32_t slen = (uint32_t)sb.size();
    std::memcpy(buf + 8, &slen, 4);
    if (slen > 0) std::memcpy(buf + 12, sb.data(), slen);
    pool->mark_dirty(SCHEMA_PAGE_ID);
}

static Schema read_meta(BufferPool* pool, uint32_t& next_rowid, uint32_t& cur_heap_page) {
    char* buf = pool->get_page(SCHEMA_PAGE_ID);
    std::memcpy(&next_rowid,    buf,     4);
    std::memcpy(&cur_heap_page, buf + 4, 4);
    uint32_t slen = 0;
    std::memcpy(&slen, buf + 8, 4);
    Schema s;
    if (slen > 0) s = Schema::deserialize(buf + 12, slen);
    return s;
}

// ── Table ────────────────────────────────────────────────────────────────────

Table::Table(BufferPool* pool, const Schema& schema, bool create)
    : pool_(pool), schema_(schema), next_rowid_(1), cur_heap_page_(HEAP_PAGE_BASE) {

    btree_ = new BTree(pool_);

    if (create) {
        schema_.table_name = schema.table_name;
        SlottedPage::init(pool_->get_page(HEAP_PAGE_BASE));
        pool_->mark_dirty(HEAP_PAGE_BASE);
        write_meta(pool_, schema_, next_rowid_, cur_heap_page_);
        pool_->flush_all(); // ensure schema page hits disk immediately
    } else {
        schema_ = read_meta(pool_, next_rowid_, cur_heap_page_);
        schema_.table_name = schema.table_name;
    }
}

Table::~Table() {
    // Always flush the latest meta before the buffer pool is torn down.
    write_meta(pool_, schema_, next_rowid_, cur_heap_page_);
    pool_->flush_all();
    delete btree_;
}

char* Table::heap_page(uint32_t page_id) {
    char* buf = pool_->get_page(page_id);
    SlottedPageHeader* hdr = reinterpret_cast<SlottedPageHeader*>(buf);
    if (hdr->free_offset == 0) {
        SlottedPage::init(buf);
        pool_->mark_dirty(page_id);
    }
    return buf;
}

uint32_t Table::insert_row(const Row& row) {
    auto bytes = serialize_row(schema_, row);
    uint16_t len = (uint16_t)bytes.size();

    char* hbuf = heap_page(cur_heap_page_);
    SlottedPage sp(hbuf);

    if (sp.free_space() < len + sizeof(SlotEntry) + 4) {
        cur_heap_page_++;
        if (cur_heap_page_ >= SCHEMA_PAGE_ID) throw std::runtime_error("out of heap pages");
        hbuf = heap_page(cur_heap_page_);
        sp   = SlottedPage(hbuf);
    }

    uint16_t slot = sp.insert(bytes.data(), len);
    if (slot == std::numeric_limits<uint16_t>::max())
        throw std::runtime_error("slot insert failed");
    pool_->mark_dirty(cur_heap_page_);

    uint32_t rowid = next_rowid_++;
    uint32_t ptr   = pack_ptr(cur_heap_page_, slot);
    btree_->insert(rowid, ptr);

    // Write meta on every insert so next_rowid_ is always current on disk.
    write_meta(pool_, schema_, next_rowid_, cur_heap_page_);

    return rowid;
}

uint32_t Table::scan(const std::function<bool(uint32_t, const Row&)>& cb) const {
    uint32_t count = 0;
    for (uint32_t rid = 1; rid < next_rowid_; ++rid) {
        int32_t packed = btree_->find(rid);
        if (packed < 0) continue;

        uint32_t pg   = ptr_page((uint32_t)packed);
        uint16_t sl   = ptr_slot((uint32_t)packed);
        char*    hbuf = pool_->get_page(pg);
        SlottedPage sp(hbuf);
        uint16_t rlen = 0;
        const char* rdata = sp.get(sl, rlen);
        if (!rdata) continue;

        Row row = deserialize_row(schema_, rdata, rlen);
        ++count;
        if (!cb(rid, row)) break;
    }
    return count;
}

bool Table::delete_row(uint32_t rowid) {
    // Resolve the heap pointer before tombstoning the BTree entry.
    int32_t packed = btree_->find(rowid);
    if (packed < 0) return false; // already gone or never existed

    // Free the slot on the heap page.
    uint32_t pg  = ptr_page((uint32_t)packed);
    uint16_t sl  = ptr_slot((uint32_t)packed);
    char*  hbuf  = pool_->get_page(pg);
    SlottedPage sp(hbuf);
    sp.delete_slot(sl);
    pool_->mark_dirty(pg);

    // Tombstone the BTree entry so future find/scan skip it.
    btree_->remove(rowid);
    return true;
}

uint32_t Table::delete_where(const std::function<bool(uint32_t, const Row&)>& pred) {
    uint32_t count = 0;
    // Collect rowids first to avoid mutating while iterating.
    std::vector<uint32_t> to_delete;
    scan([&](uint32_t rid, const Row& row) -> bool {
        if (pred(rid, row)) to_delete.push_back(rid);
        return true; // keep scanning
    });
    for (uint32_t rid : to_delete) {
        if (delete_row(rid)) ++count;
    }
    return count;
}

bool Table::find_by_rowid(uint32_t rowid, Row& out) const {
    int32_t packed = btree_->find(rowid);
    if (packed < 0) return false;
    uint32_t pg   = ptr_page((uint32_t)packed);
    uint16_t sl   = ptr_slot((uint32_t)packed);
    char*    hbuf = pool_->get_page(pg);
    SlottedPage sp(hbuf);
    uint16_t rlen = 0;
    const char* rdata = sp.get(sl, rlen);
    if (!rdata) return false;
    out = deserialize_row(schema_, rdata, rlen);
    return true;
}