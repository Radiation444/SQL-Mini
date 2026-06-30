#ifndef BTREE_H
#define BTREE_H
#include <vector>
#include <utility>
#include "buffer_pool.h"
#include "node.h"

const uint32_t BTREE_TOMBSTONE = 0xFFFFFFFE;

class BTree {
    BufferPool* pool;
    uint32_t root_page_id;
    uint32_t leaf_node_find(Node* node, uint32_t key);
    void create_new_root(Node* old, uint32_t old_id, uint32_t right_id);
    void leaf_node_split_and_insert(Node* old, uint32_t old_id, uint32_t k, uint32_t v,
                                    std::vector<std::pair<uint32_t,uint32_t>>& ancestors);
    void insert_into_parent(std::vector<std::pair<uint32_t,uint32_t>>& ancestors,
                            uint32_t sep_key, uint32_t right_id);
    void internal_node_split_and_insert(uint32_t node_id, uint32_t sep_key, uint32_t right_child_id,
                                        std::vector<std::pair<uint32_t,uint32_t>>& ancestors);
public:
    BTree(BufferPool* p);
    void insert(uint32_t k, uint32_t v);
    int32_t find(uint32_t k);
    bool remove(uint32_t k);
};
#endif