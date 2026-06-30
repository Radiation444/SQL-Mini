#include "node.h"
#include <cstring>

Node::Node(char* buf) : buffer(buf) {}

// Header field accessors — all offsets match the 16-byte aligned layout in node.h:
//   [0]     node_type
//   [1]     is_root
//   [2-3]   padding
//   [4-7]   num_cells   (4-byte aligned)
//   [8-11]  rightmost_child_id / next_leaf_id  (4-byte aligned)
//   [12-15] padding
// Cells start at offset 16 (4-byte aligned); each cell is 8 bytes (key + value).

NodeType Node::get_node_type() { return static_cast<NodeType>(static_cast<uint8_t>(buffer[0])); }
void Node::set_node_type(NodeType t) { buffer[0] = static_cast<uint8_t>(t); }

bool Node::is_root() { return static_cast<bool>(buffer[1]); }
void Node::set_is_root(bool r) { buffer[1] = static_cast<char>(r); }

uint32_t Node::get_num_cells() {
    uint32_t n;
    std::memcpy(&n, buffer + 4, sizeof(uint32_t));
    return n;
}
void Node::set_num_cells(uint32_t n) {
    std::memcpy(buffer + 4, &n, sizeof(uint32_t));
}
void Node::increment_num_cells() { set_num_cells(get_num_cells() + 1); }

uint32_t Node::get_rightmost_child_id() {
    uint32_t id;
    std::memcpy(&id, buffer + 8, sizeof(uint32_t));
    return id;
}
void Node::set_rightmost_child_id(uint32_t id) {
    std::memcpy(buffer + 8, &id, sizeof(uint32_t));
}
void Node::set_next_leaf_id(uint32_t id) {
    std::memcpy(buffer + 8, &id, sizeof(uint32_t));
}
uint32_t Node::get_next_leaf_id() {
    uint32_t id;
    std::memcpy(&id, buffer + 8, sizeof(uint32_t));
    return id;
}

uint32_t Node::get_cell_offset(uint32_t i) { return NODE_HEADER_SIZE + (i * CELL_SIZE); }

uint32_t Node::get_key(uint32_t i) {
    uint32_t k;
    std::memcpy(&k, buffer + get_cell_offset(i), sizeof(uint32_t));
    return k;
}
void Node::set_key(uint32_t i, uint32_t k) {
    std::memcpy(buffer + get_cell_offset(i), &k, sizeof(uint32_t));
}
uint32_t Node::get_value(uint32_t i) {
    uint32_t v;
    std::memcpy(&v, buffer + get_cell_offset(i) + 4, sizeof(uint32_t));
    return v;
}
void Node::set_value(uint32_t i, uint32_t v) {
    std::memcpy(buffer + get_cell_offset(i) + 4, &v, sizeof(uint32_t));
}

uint32_t Node::get_left_child_id(uint32_t i) { return get_value(i); }
void Node::set_left_child_id(uint32_t i, uint32_t id) { set_value(i, id); }

void Node::initialize_as_leaf(bool r) {
    set_node_type(NodeType::LEAF);
    set_is_root(r);
    set_num_cells(0);
    set_next_leaf_id(INVALID_PAGE_ID);
}
void Node::initialize_as_internal(bool r) {
    set_node_type(NodeType::INTERNAL);
    set_is_root(r);
    set_num_cells(0);
}

char* Node::get_raw_buffer() { return buffer; }