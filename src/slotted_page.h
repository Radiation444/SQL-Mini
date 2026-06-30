#ifndef SLOTTED_PAGE_H
#define SLOTTED_PAGE_H
#include "pager.h"
#include <cstdint>

/*
 * Slotted Page Layout (PAGE_SIZE bytes)
 * ──────────────────────────────────────
 *  [0-1]   num_slots   (uint16_t)
 *  [2-3]   free_offset (uint16_t) — next free byte from END of page (grows left)
 *  [4 ..]  slot directory: num_slots × SlotEntry (4 bytes each)
 *           slot[i].offset  (uint16_t) — byte offset from page start; 0 = deleted
 *           slot[i].length  (uint16_t) — byte length of record
 *  [.. end] record data packed from the end of the page, growing left
 *
 * A "slot id" is a stable index into the slot directory.
 * Deleted slots have offset == 0 and can be reused.
 */

const uint16_t SLOT_DELETED = 0;

struct SlotEntry {
    uint16_t offset;  // from page start; 0 means deleted
    uint16_t length;
};

// Header at the very start of a slotted page buffer.
struct SlottedPageHeader {
    uint16_t num_slots;
    uint16_t free_offset; // next free position from END (data grows leftward)
};

// All operations work on a raw PAGE_SIZE buffer (from BufferPool).
class SlottedPage {
public:
    explicit SlottedPage(char* buf);

    // Returns slot index (stable id), or UINT16_MAX on full page.
    uint16_t insert(const char* data, uint16_t len);

    // Returns pointer + length into the page buffer. nullptr if deleted/invalid.
    const char* get(uint16_t slot, uint16_t& out_len) const;

    // Mark a slot as deleted (space is not reclaimed without compaction).
    void delete_slot(uint16_t slot);

    // Update a slot in-place; if new data fits in old space, reuse it.
    // Otherwise deletes old + inserts new (slot id changes — caller must update index).
    uint16_t update(uint16_t slot, const char* data, uint16_t len);

    uint16_t num_slots() const;
    uint16_t free_space() const; // bytes available for new data + slot entry

    static void init(char* buf); // zero-initialise a fresh page

private:
    char* buf_;
    SlottedPageHeader* hdr();
    const SlottedPageHeader* hdr() const;
    SlotEntry* slot_dir();
    const SlotEntry* slot_dir() const;
    uint16_t data_start() const; // first byte used by record data
};

#endif
