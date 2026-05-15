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
#define MAX_TABLE_PAGES 1000

#define INTERNAL_NODE_MAX_KEYS 248
#define LEAF_NODE_MAX_KEYS 248

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
    EXECUTE_TABLE_FULL,
    EXECUTE_TABLE_EMPTY,
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
    STATEMENT_SELECT,
    STATEMENT_INSERT_BULK,
    STATEMENT_DELETE_BULK,
    STATEMENT_DELETE
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
    Row target_row;
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
    PageID latest_page;
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

typedef uint32_t PageID;
typedef uint32_t Key;

typedef enum
{
    NODE_INTERNAL,
    NODE_LEAF
} NodeType;

typedef struct
{
    PageID page_id;
    uint32_t page_byte_offset;
} RowID;

typedef struct
{
    RowID row_id;
    void *row_loc;
} RowSlot;

typedef struct
{
    uint32_t num_keys;
    Key keys[INTERNAL_NODE_MAX_KEYS + 1]; // leave space for +1 overflow before splitting
    PageID page_ids[INTERNAL_NODE_MAX_KEYS + 2];
} InternalNode;

typedef struct
{
    uint32_t num_keys;
    Key keys[LEAF_NODE_MAX_KEYS + 1]; // leave space for +1 overflow before splitting
    RowID row_ids[LEAF_NODE_MAX_KEYS + 1];
    PageID next_leaf;
} LeafNode;

typedef struct
{
    bool split;
    Key promoted_key;
    PageID new_right_node;
} InsertResult;

typedef struct
{
    bool underfilled; // tells us if a child node must borrow elements...
    bool DNE;         // use if the key does not exist. lets the recursive algor know to collapse
    bool manipulate_leaf;
} DeleteResult;

typedef struct
{
    bool exists;
    Key key;
} SearchResult;

// Sizing constants (extern in types.c)
extern const uint32_t PAGE_SIZE;
extern const uint32_t HEADER_SIZE;
extern const uint32_t ID_SIZE;
extern const uint32_t USERNAME_SIZE;
extern const uint32_t EMAIL_SIZE;
extern const uint32_t ID_OFFSET;
extern const uint32_t USERNAME_OFFSET;
extern const uint32_t EMAIL_OFFSET;
extern const uint32_t ROW_SIZE;
extern const uint32_t ROWS_PER_PAGE;
extern const uint32_t TABLE_MAX_ROWS;
extern const uint32_t MIN_KEYS_INTERNAL;
extern const uint32_t MIN_KEYS_LEAF;
#endif