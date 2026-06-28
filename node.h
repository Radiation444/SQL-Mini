#ifndef NODE_H
#define NODE_H
#include <cstdint>
#include "pager.h"

const uint32_t INVALID_PAGE_ID = 0xFFFFFFFF;

// Header layout (16 bytes, all multi-byte fields are 4-byte aligned):
//   [0]     node_type  (1 byte)
//   [1]     is_root    (1 byte)
//   [2-3]   padding
//   [4-7]   num_cells  (uint32_t)
//   [8-11]  rightmost_child_id / next_leaf_id  (uint32_t)
//   [12-15] padding
const uint32_t NODE_HEADER_SIZE = 16;
const uint32_t CELL_SIZE = 8; // 4 byte key + 4 byte value/child
const uint32_t NODE_MAX_CELLS = (PAGE_SIZE - NODE_HEADER_SIZE) / CELL_SIZE;

enum class NodeType : uint8_t { INTERNAL = 0, LEAF = 1 };

class Node {
private:
    char* buffer;
public:
    Node(char* buf);
    NodeType get_node_type(); void set_node_type(NodeType t);
    bool is_root(); void set_is_root(bool r);
    uint32_t get_num_cells(); void set_num_cells(uint32_t n);
    void increment_num_cells();
    uint32_t get_key(uint32_t i); void set_key(uint32_t i, uint32_t k);
    uint32_t get_value(uint32_t i); void set_value(uint32_t i, uint32_t v);
    uint32_t get_left_child_id(uint32_t i); void set_left_child_id(uint32_t i, uint32_t id);
    uint32_t get_rightmost_child_id(); void set_rightmost_child_id(uint32_t id);
    void set_next_leaf_id(uint32_t id); uint32_t get_next_leaf_id();
    void set_parent_id(uint32_t id); uint32_t get_parent_id();
    void initialize_as_leaf(bool r); void initialize_as_internal(bool r);
    char* get_raw_buffer();
    uint32_t get_cell_offset(uint32_t i);
};
#endif