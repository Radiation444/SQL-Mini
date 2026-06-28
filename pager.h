#ifndef PAGER_H
#define PAGER_H
#include <iostream>
#include <fstream>
#include <string>
#include <cstdint>

const uint32_t PAGE_SIZE = 4096;

class Pager {
private:
    std::fstream file;
    uint32_t num_pages;
public:
    Pager(const std::string& filename);
    ~Pager();
    void read_page(uint32_t page_num, char* buffer);
    void write_page(uint32_t page_num, const char* buffer);
    uint32_t get_num_pages();
};
#endif