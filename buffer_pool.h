#ifndef BUFFER_POOL_H
#define BUFFER_POOL_H
#include "pager.h"
#include <unordered_map>
#include <list>

class BufferPool {
    struct PageEntry { uint32_t page_num; char buffer[PAGE_SIZE]; bool dirty; };
    Pager* pager; uint32_t max_pages;
    std::list<PageEntry> lru;
    std::unordered_map<uint32_t, std::list<PageEntry>::iterator> cache;
public:
    uint32_t next_page_id;
    BufferPool(Pager* p, uint32_t max) : pager(p), max_pages(max) {
        next_page_id = pager->get_num_pages();
    }
    ~BufferPool() { flush_all(); }
    char* get_page(uint32_t page_num);
    void mark_dirty(uint32_t page_num);
    void flush_all();
    uint32_t get_unused_page_num() { return next_page_id++; }
    uint32_t get_num_pages() const { return pager->get_num_pages(); }
};
#endif