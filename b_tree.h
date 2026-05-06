#ifndef B_TREE_H
#define B_TREE_H

#include "types.h"

// Function declarations from b_tree.c
InsertResult insert_key(Table *table, PageID root_leaf_page_id, Key key_to_insert, RowID *row_slot);
InsertResult split_node_leaf(LeafNode *node, Key *leaf_keys, RowID *leaf_rows, Table *table, RowID *row_slot);
InsertResult split_node_internal(InternalNode *node, Key *keys, PageID *pages, Table *table, RowID *row_slot);
uint32_t array_insert_key_row_pair(Key *key_arr, RowID *row_arr, Key key_value, RowID row_value, uint32_t *key_arr_len, uint32_t *row_arr_len);
uint32_t array_insert_key(Key *key_arr, Key key_value, uint32_t *key_arr_len);

// Function declarations from db.c (used by b_tree.c)
PageHeader *get_page(Pager *pager, PageID page_num);
RowSlot *get_row_slot(Pager *pager, uint32_t page_num, uint32_t row_num_offset);
void writeRow(Row *source, void *dest);
void readRow(void *source, Row *dest);
uint32_t allocate_page_leaf(Table *table, PageHeader *page_header, LeafNode *node);
uint32_t allocate_page_internal(Table *table, PageHeader *page_header, InternalNode *node);

// Command declarations
PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_statement(Statement *statement, Table *table);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);

#endif