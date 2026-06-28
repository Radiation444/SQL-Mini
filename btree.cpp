#include "btree.h"
#include "buffer_pool.h"
#include "pager.h"
#include <cstring>
#include <cstdint>
#include <vector>
#include <utility>
#include <algorithm>

BTree::BTree(BufferPool* p) : pool(p), root_page_id(0) {
    bool is_new = (pool->get_num_pages() == 0);
    char* buf = pool->get_page(0);
    if (is_new) {
        Node root(buf);
        root.initialize_as_leaf(true);
        pool->mark_dirty(0);
    }
}

uint32_t BTree::leaf_node_find(Node* node, uint32_t key) {
    uint32_t min = 0, max = node->get_num_cells();
    while (min != max) {
        uint32_t mid = (min + max) / 2;
        if (key == node->get_key(mid)) return mid;
        if (key < node->get_key(mid)) max = mid;
        else min = mid + 1;
    }
    return min;
}

void BTree::create_new_root(Node* old, uint32_t old_id, uint32_t right_id) {
    uint32_t left_id = pool->get_unused_page_num();
    char* left_buf = pool->get_page(left_id);
    std::memcpy(left_buf, old->get_raw_buffer(), PAGE_SIZE);
    Node left(left_buf); left.set_is_root(false);
    pool->mark_dirty(left_id);

    old->initialize_as_internal(true);
    old->set_num_cells(1);
    
    Node right(pool->get_page(right_id));
    old->set_left_child_id(0, left_id);
    old->set_key(0, right.get_key(0));
    old->set_rightmost_child_id(right_id);
    pool->mark_dirty(old_id);
}

void BTree::leaf_node_split_and_insert(Node* old, uint32_t old_id, uint32_t k, uint32_t v,
                                       std::vector<std::pair<uint32_t,uint32_t>>& ancestors) {
    uint32_t new_id = pool->get_unused_page_num();
    Node new_node(pool->get_page(new_id));
    new_node.initialize_as_leaf(false);
    new_node.set_next_leaf_id(old->get_next_leaf_id());
    old->set_next_leaf_id(new_id);

    uint32_t split = (NODE_MAX_CELLS + 1) / 2;
    uint32_t ins   = leaf_node_find(old, k);

    for (int32_t i = NODE_MAX_CELLS; i >= 0; i--) {
        Node*    target = (i >= (int32_t)split) ? &new_node : old;
        uint32_t idx    = (i >= (int32_t)split) ? (i - split) : i;
        if      (i == (int32_t)ins) { target->set_key(idx, k);              target->set_value(idx, v); }
        else if (i >  (int32_t)ins) { target->set_key(idx, old->get_key(i-1)); target->set_value(idx, old->get_value(i-1)); }
        else                        { target->set_key(idx, old->get_key(i));   target->set_value(idx, old->get_value(i)); }
    }
    old->set_num_cells(split);
    new_node.set_num_cells(NODE_MAX_CELLS + 1 - split);
    pool->mark_dirty(old_id);
    pool->mark_dirty(new_id);

    uint32_t separator = new_node.get_key(0);

    if (old->is_root()) {
        create_new_root(old, old_id, new_id);
    } else {
        insert_into_parent(ancestors, separator, new_id);
    }
}


void BTree::insert(uint32_t key, uint32_t value) {
    uint32_t cur_id = root_page_id;
    Node node(pool->get_page(cur_id));
    std::vector<std::pair<uint32_t,uint32_t>> ancestors;

    while (node.get_node_type() == NodeType::INTERNAL) {
        uint32_t next_id = node.get_rightmost_child_id();
        uint32_t slot    = node.get_num_cells(); // default: rightmost

        for (uint32_t i = 0; i < node.get_num_cells(); ++i) {
            if (key < node.get_key(i)) {
                next_id = node.get_left_child_id(i);
                slot    = i;
                break;
            }
        }
        ancestors.push_back({cur_id, slot});
        cur_id = next_id;
        node   = Node(pool->get_page(cur_id));
    }

    if (node.get_num_cells() >= NODE_MAX_CELLS) {
        leaf_node_split_and_insert(&node, cur_id, key, value, ancestors);
    } else {
        uint32_t idx = leaf_node_find(&node, key);
        if (idx < node.get_num_cells() && node.get_key(idx) == key) {
            node.set_value(idx, value);
        } else {
            for (uint32_t i = node.get_num_cells(); i > idx; i--) {
                node.set_key(i,   node.get_key(i - 1));
                node.set_value(i, node.get_value(i - 1));
            }
            node.set_key(idx, key);
            node.set_value(idx, value);
            node.increment_num_cells();
        }
        pool->mark_dirty(cur_id);
    }
}

void BTree::insert_into_parent(std::vector<std::pair<uint32_t,uint32_t>>& ancestors,
                               uint32_t sep_key, uint32_t right_id) {
    auto [parent_id, slot] = ancestors.back();
    ancestors.pop_back();

    Node parent(pool->get_page(parent_id));
    uint32_t num = parent.get_num_cells();

    if (num < NODE_MAX_CELLS) {
        uint32_t old_rightmost = parent.get_rightmost_child_id();
        for (uint32_t i = num; i > slot; i--) {
            parent.set_key(i,            parent.get_key(i - 1));
            parent.set_left_child_id(i,  parent.get_left_child_id(i - 1));
        }

        parent.set_key(slot, sep_key);

        if (slot < num) {
            parent.set_left_child_id(slot + 1, right_id);
        } else {

            parent.set_left_child_id(slot, old_rightmost);
            parent.set_rightmost_child_id(right_id);
        }

        parent.set_num_cells(num + 1);
        pool->mark_dirty(parent_id);
    } else {
        internal_node_split_and_insert(parent_id, sep_key, right_id, ancestors);
    }
}

void BTree::internal_node_split_and_insert(uint32_t node_id, uint32_t sep_key, uint32_t right_child_id,
                                           std::vector<std::pair<uint32_t,uint32_t>>& ancestors) {
    Node node(pool->get_page(node_id));
    bool     was_root = node.is_root();
    uint32_t N        = NODE_MAX_CELLS;

    std::vector<uint32_t> old_keys(N), old_ch(N + 1);
    for (uint32_t i = 0; i < N; ++i) {
        old_keys[i] = node.get_key(i);
        old_ch[i]   = node.get_left_child_id(i);
    }
    old_ch[N] = node.get_rightmost_child_id();

    // Find where sep_key belongs.
    uint32_t slot = N;
    for (uint32_t i = 0; i < N; ++i) {
        if (sep_key < old_keys[i]) { slot = i; break; }
    }

    // Build the merged N+1-key / N+2-child arrays.
    std::vector<uint32_t> new_keys(N + 1), new_ch(N + 2);
    for (uint32_t i = 0; i < slot; ++i) {
        new_keys[i] = old_keys[i];
        new_ch[i]   = old_ch[i];
    }
    new_keys[slot]     = sep_key;
    new_ch[slot]       = old_ch[slot];      // left half of the child that was split
    new_ch[slot + 1]   = right_child_id;    // right half
    for (uint32_t i = slot; i < N; ++i) {
        new_keys[i + 1] = old_keys[i];
        new_ch[i + 2]   = old_ch[i + 1];
    }

    // Split: left keeps [0..M-1], push up [M], right keeps [M+1..N].
    uint32_t M          = (N + 1) / 2;
    uint32_t pushed_key = new_keys[M];
    uint32_t left_num   = M;
    uint32_t right_num  = N - M;

    // Write left half into node_id (overwrite in place).
    node.initialize_as_internal(false); // is_root fixed up below
    node.set_num_cells(left_num);
    for (uint32_t i = 0; i < left_num; ++i) {
        node.set_key(i,           new_keys[i]);
        node.set_left_child_id(i, new_ch[i]);
    }
    node.set_rightmost_child_id(new_ch[left_num]);

    // Allocate right sibling and write its half.
    uint32_t right_id = pool->get_unused_page_num();
    Node right_node(pool->get_page(right_id));
    right_node.initialize_as_internal(false);
    right_node.set_num_cells(right_num);
    for (uint32_t i = 0; i < right_num; ++i) {
        right_node.set_key(i,           new_keys[M + 1 + i]);
        right_node.set_left_child_id(i, new_ch[M + 1 + i]);
    }
    right_node.set_rightmost_child_id(new_ch[M + 1 + right_num]);
    pool->mark_dirty(right_id);

    if (was_root) {
        // Root must stay at page 0.  Copy the left half to a fresh page,
        // then reinitialise page 0 as the new single-key root.
        uint32_t left_id = pool->get_unused_page_num();
        char*    left_buf = pool->get_page(left_id);
        std::memcpy(left_buf, node.get_raw_buffer(), PAGE_SIZE);
        Node left_copy(left_buf);
        left_copy.set_is_root(false);
        pool->mark_dirty(left_id);

        node.initialize_as_internal(true);
        node.set_num_cells(1);
        node.set_left_child_id(0, left_id);
        node.set_key(0, pushed_key);
        node.set_rightmost_child_id(right_id);
        pool->mark_dirty(node_id);
    } else {
        node.set_is_root(false);
        pool->mark_dirty(node_id);
        insert_into_parent(ancestors, pushed_key, right_id);
    }
}

bool BTree::remove(uint32_t k) {
    uint32_t cur = root_page_id;
    Node node(pool->get_page(cur));
    while (node.get_node_type() == NodeType::INTERNAL) {
        uint32_t next = node.get_rightmost_child_id();
        for (uint32_t i = 0; i < node.get_num_cells(); ++i) {
            if (k < node.get_key(i)) { next = node.get_left_child_id(i); break; }
        }
        node = Node(pool->get_page(next));
        cur  = next;
    }
    uint32_t idx = leaf_node_find(&node, k);
    if (idx >= node.get_num_cells() || node.get_key(idx) != k) return false;
    if (node.get_value(idx) == BTREE_TOMBSTONE) return false; // already deleted
    node.set_value(idx, BTREE_TOMBSTONE);
    pool->mark_dirty(cur);
    return true;
}

int32_t BTree::find(uint32_t k) {
    uint32_t cur = root_page_id;
    Node node(pool->get_page(cur));
    while(node.get_node_type() == NodeType::INTERNAL) {
        uint32_t next = node.get_rightmost_child_id();
        for(uint32_t i=0; i<node.get_num_cells(); ++i) {
            if(k < node.get_key(i)) { next = node.get_left_child_id(i); break; }
        }
        node = Node(pool->get_page(next));
    }
    uint32_t idx = leaf_node_find(&node, k);
    if (idx < node.get_num_cells() && node.get_key(idx) == k) {
        uint32_t val = node.get_value(idx);
        return (val == BTREE_TOMBSTONE) ? -1 : (int32_t)val;
    }
    return -1;
}