#include "slotted_page.h"
#include <cstring>
#include <cassert>
#include <limits>

// ── helpers ─────────────────────────────────────────────────────────────────

SlottedPage::SlottedPage(char* buf) : buf_(buf) {}

void SlottedPage::init(char* buf) {
    std::memset(buf, 0, PAGE_SIZE);
    SlottedPageHeader* h = reinterpret_cast<SlottedPageHeader*>(buf);
    h->num_slots   = 0;
    h->free_offset = PAGE_SIZE; // data starts at end, grows left
}

SlottedPageHeader* SlottedPage::hdr() {
    return reinterpret_cast<SlottedPageHeader*>(buf_);
}
const SlottedPageHeader* SlottedPage::hdr() const {
    return reinterpret_cast<const SlottedPageHeader*>(buf_);
}

SlotEntry* SlottedPage::slot_dir() {
    return reinterpret_cast<SlotEntry*>(buf_ + sizeof(SlottedPageHeader));
}
const SlotEntry* SlottedPage::slot_dir() const {
    return reinterpret_cast<const SlotEntry*>(buf_ + sizeof(SlottedPageHeader));
}

// Byte offset of the first byte occupied by slot-directory entries.
// (slot directory is at a fixed position right after the header)
uint16_t SlottedPage::data_start() const {
    return static_cast<uint16_t>(sizeof(SlottedPageHeader)
                                 + hdr()->num_slots * sizeof(SlotEntry));
}

// ── public API ──────────────────────────────────────────────────────────────

uint16_t SlottedPage::num_slots() const { return hdr()->num_slots; }

uint16_t SlottedPage::free_space() const {
    // Space = gap between end of slot directory and start of record data.
    // We need room for the record itself plus one new SlotEntry (if no deleted
    // slot is available to reuse).
    uint16_t dir_end = static_cast<uint16_t>(sizeof(SlottedPageHeader)
                                              + hdr()->num_slots * sizeof(SlotEntry));
    if (hdr()->free_offset <= dir_end) return 0;
    uint16_t gap = hdr()->free_offset - dir_end;
    return gap;
}

uint16_t SlottedPage::insert(const char* data, uint16_t len) {
    // Minimum space needed: record bytes + possibly a new slot directory entry.
    uint16_t need_data = len;
    uint16_t need_slot = static_cast<uint16_t>(sizeof(SlotEntry));

    // Try to find a deleted slot to reuse (avoids growing directory).
    int16_t reuse = -1;
    SlotEntry* dir = slot_dir();
    for (uint16_t i = 0; i < hdr()->num_slots; ++i) {
        if (dir[i].offset == SLOT_DELETED) { reuse = (int16_t)i; break; }
    }

    uint16_t extra = (reuse == -1) ? need_slot : 0;

    uint16_t dir_end = static_cast<uint16_t>(sizeof(SlottedPageHeader)
                                              + hdr()->num_slots * sizeof(SlotEntry) + extra);
    if (hdr()->free_offset < dir_end + need_data) {
        return std::numeric_limits<uint16_t>::max(); // page full
    }

    // Allocate from the end.
    hdr()->free_offset -= len;
    uint16_t record_offset = hdr()->free_offset;
    std::memcpy(buf_ + record_offset, data, len);

    uint16_t slot_idx;
    if (reuse != -1) {
        slot_idx = (uint16_t)reuse;
    } else {
        slot_idx = hdr()->num_slots;
        hdr()->num_slots++;
        // Re-fetch dir pointer after header mutation (same buffer, just alias).
    }
    dir = slot_dir(); // re-fetch in case num_slots changed
    dir[slot_idx].offset = record_offset;
    dir[slot_idx].length = len;
    return slot_idx;
}

const char* SlottedPage::get(uint16_t slot, uint16_t& out_len) const {
    if (slot >= hdr()->num_slots) return nullptr;
    const SlotEntry* dir = slot_dir();
    if (dir[slot].offset == SLOT_DELETED) return nullptr;
    out_len = dir[slot].length;
    return buf_ + dir[slot].offset;
}

void SlottedPage::delete_slot(uint16_t slot) {
    if (slot >= hdr()->num_slots) return;
    slot_dir()[slot].offset = SLOT_DELETED;
    slot_dir()[slot].length = 0;
}

uint16_t SlottedPage::update(uint16_t slot, const char* data, uint16_t len) {
    if (slot < hdr()->num_slots) {
        SlotEntry& e = slot_dir()[slot];
        if (e.offset != SLOT_DELETED && e.length >= len) {
            // Fits in-place (may waste a few bytes — acceptable).
            std::memcpy(buf_ + e.offset, data, len);
            e.length = len;
            return slot;
        }
        // Delete old slot and fall through to a fresh insert.
        delete_slot(slot);
    }
    return insert(data, len);
}
