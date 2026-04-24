#ifndef B_TREE_H
#define B_TREE_H

#include <stdint.h>

typedef uint32_t PageID;
typedef uint32_t Key;

#define INTERNAL_NODE_MAX_KEYS 128
#define LEAF_NODE_MAX_KEYS 32

typedef enum
{
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

typedef struct
{
    PageID page_id;
    uint32_t offset;
} RowID;

typedef struct
{
    RowID row_id;
    void *row_loc;
} RowSlot;

typedef struct
{
    NodeType type;
    uint32_t num_keys;
    Key keys[INTERNAL_NODE_MAX_KEYS];
    PageID page_ids[INTERNAL_NODE_MAX_KEYS + 1];
} InternalNode;

typedef struct
{
    NodeType type;
    uint32_t num_keys;
    Key keys[LEAF_NODE_MAX_KEYS];
    RowID row_ids[LEAF_NODE_MAX_KEYS];
    PageID next_leaf;
} LeafNode;

#endif
