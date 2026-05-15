#ifndef OPERATIONS_H
#define OPERATIONS_H

#include "types.h"

void writeRow(Row *source, void *dest);
void readRow(void *source, Row *dest);
InputBuffer *createInputBuffer();
char *getInput(InputBuffer *input_buffer);
Pager *open_pager(char *filename);
int execute_meta_command(char *buffer, Table *table);
PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_statement(Statement *statement, Table *table);
PageHeader *get_page(Pager *pager, PageID page_num);
RowSlot get_row_slot(Pager *pager, uint32_t page_num, uint32_t row_num_offset);
uint32_t allocate_page_leaf(Table *table, PageHeader *page_header, LeafNode *node);
uint32_t allocate_page_internal(Table *table, PageHeader *page_header, InternalNode *node);
void db_close(Table *table);
void pager_flush(Pager *pager, uint32_t page_num, uint32_t size);

#endif