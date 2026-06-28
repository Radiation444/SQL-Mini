#include "buffer_pool.h"
#include <cstring>
char* BufferPool::get_page(uint32_t page_num) {
    // Keep next_page_id beyond every page that has ever been accessed, so that
    // get_unused_page_num() never hands out a page that is already in use.
    if (page_num >= next_page_id)
        next_page_id = page_num + 1;

    if (cache.find(page_num) != cache.end()) {
        lru.splice(lru.begin(), lru, cache[page_num]);
        return cache[page_num]->buffer;
    }

    if (lru.size() >= max_pages) {
        PageEntry& oldest = lru.back();        
        if (oldest.dirty) pager->write_page(oldest.page_num, oldest.buffer);
        cache.erase(oldest.page_num);
        lru.pop_back();
    }

    lru.push_front({page_num, {0}, false});
    pager->read_page(page_num, lru.front().buffer);
    cache[page_num] = lru.begin();
    return lru.front().buffer;
}   
void BufferPool::mark_dirty(uint32_t p) { cache[p]->dirty = true; }
void BufferPool::flush_all() {
    for (auto& p : lru) {
        if (p.dirty) {
            pager->write_page(p.page_num, p.buffer);
        }
    }
}