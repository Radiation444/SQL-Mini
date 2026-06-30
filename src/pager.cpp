#include "pager.h"
#include <stdexcept>
#include <cstring>

Pager::Pager(const std::string& filename) {
    file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    if (!file) {
        file.clear();
        file.open(filename, std::ios::out | std::ios::binary);
        file.close();
        file.open(filename, std::ios::in | std::ios::out | std::ios::binary);
    }
    file.seekg(0, std::ios::end);
    num_pages = static_cast<uint32_t>(file.tellg() / PAGE_SIZE);
}

Pager::~Pager() {
    if (file.is_open()) {
        file.flush();
        file.close();
    }
}

void Pager::read_page(uint32_t page_num, char* buffer) {

    std::memset(buffer, 0, PAGE_SIZE);
    file.seekg(static_cast<std::streamoff>(page_num) * PAGE_SIZE, std::ios::beg);
    file.read(buffer, PAGE_SIZE);

    if (!file) file.clear();
}

void Pager::write_page(uint32_t page_num, const char* buffer) {
    file.seekp(static_cast<std::streamoff>(page_num) * PAGE_SIZE, std::ios::beg);
    file.write(buffer, PAGE_SIZE);

    if (page_num >= num_pages) num_pages = page_num + 1;
}

uint32_t Pager::get_num_pages() { return num_pages; }