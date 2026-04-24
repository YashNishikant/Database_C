#ifndef TYPES_H
#define TYPES_H

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255
#define MAX_TABLE_PAGES 100

const uint32_t PAGE_SIZE = 4096; // 4kb ✅

typedef uint32_t PageID;

typedef enum
{
    PAGE_HEAP,
    PAGE_INTERNAL,
    PAGE_LEAF
} PageType;

typedef struct
{
    PageType type;
} PageHeader;

typedef struct
{
    char *buffer;
    int buffer_size;
    int input_size;
} InputBuffer;

typedef enum
{
    EXECUTE_SUCCESS,
    EXECUTE_TABLE_FULL
} ExecuteResult;

typedef enum
{
    META_COMMAND_SUCCESS,
    META_COMMAND_UNRECOGNIZED
} MetaCommandResult;

typedef enum
{
    PREPARE_SUCCESS,
    PREPARATION_FAILURE,
    PREPARATION_SYNTAX_ERROR,
    PREPARATION_STRING_OVERFLOW,
    PREPARATION_NEGATIVE_ID
} PrepareResult;

typedef enum
{
    STATEMENT_INSERT,
    STATEMENT_SELECT
} StatementType;

typedef struct
{
    uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1]; // decays to pointers when passed to sscanf
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct
{
    StatementType type;
    Row row_to_insert;
} Statement;

typedef struct
{
    int file_descriptor;
    uint32_t file_length;
    void *pages[MAX_TABLE_PAGES];
} Pager;

typedef struct
{
    uint32_t num_rows;
    Pager *pager;
    PageID latest_heap_page;
    PageID latest_node_page;
    PageID root;
    uint32_t latest_heap_row;
} Table;

typedef struct
{
    Table *table;
    PageID page_num;
    uint32_t row_num_offset;
    bool end_of_table;
} Cursor;

const uint32_t HEADER_SIZE = sizeof(PageHeader);
const uint32_t ID_SIZE = sizeof(((Row *)0)->id);
const uint32_t USERNAME_SIZE = sizeof(((Row *)0)->username);
const uint32_t EMAIL_SIZE = sizeof(((Row *)0)->email);
const uint32_t ID_OFFSET = 0;
const uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;
const uint32_t ROWS_PER_PAGE = ((PAGE_SIZE - HEADER_SIZE) / ROW_SIZE);
const uint32_t TABLE_MAX_ROWS = ROWS_PER_PAGE * MAX_TABLE_PAGES;

#endif