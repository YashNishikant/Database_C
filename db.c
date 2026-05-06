#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "types.h"

// keys (row ids) can only start from 1 and go up
// pages (page ids) can only start from 1 and go up

char *getInput(InputBuffer *input_buffer);
InputBuffer *createInputBuffer();
PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_statement(Statement *statement, Table *table);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);
InsertResult insert_key(Table *table, PageID root_leaf_page_id, Key key_to_insert, RowID *row_slot);
uint32_t array_insert_key_row_pair(Key *key_arr, RowID *row_arr, Key key_value, RowID row_value, uint32_t *key_arr_len, uint32_t *row_arr_len);
uint32_t array_insert_key(Key *key_arr, Key key_value, uint32_t *key_arr_len);
uint32_t allocate_page_leaf(Table *table, PageHeader *page_header, LeafNode *node);
uint32_t allocate_page_internal(Table *table, PageHeader *page_header, InternalNode *node);

void advance_cursor(Cursor *cursor)
{
    cursor->row_num_offset += 1;

    if (cursor->row_num_offset >= cursor->table->num_rows)
        cursor->end_of_table = true;

    else if (cursor->row_num_offset == ROWS_PER_PAGE)
    {
        cursor->row_num_offset = 0;
        cursor->page_num++;
    }
}

void writeRow(Row *source, void *dest)
{
    memcpy(dest + ID_OFFSET, &(source->id), ID_SIZE);
    memcpy(dest + USERNAME_OFFSET, &(source->username), USERNAME_SIZE);
    memcpy(dest + EMAIL_OFFSET, &(source->email), EMAIL_SIZE);
}

void readRow(void *source, Row *dest)
{
    memcpy(&(dest->id), source + ID_OFFSET, ID_SIZE);
    memcpy(&(dest->username), source + USERNAME_OFFSET, USERNAME_SIZE);
    memcpy(&(dest->email), source + EMAIL_OFFSET, EMAIL_SIZE);
}

InputBuffer *createInputBuffer()
{

    InputBuffer *user_input = (InputBuffer *)malloc(sizeof(InputBuffer));
    user_input->buffer = NULL;
    user_input->buffer_size = 0;
    user_input->input_size = 0;

    return user_input;
}

char *getInput(InputBuffer *input_buffer)
{
    getline(&(input_buffer->buffer), &(input_buffer->buffer_size), stdin);
    input_buffer->input_size = strlen(input_buffer->buffer) - 1;
    input_buffer->buffer[input_buffer->input_size] = 0;
}

Pager *open_pager(char *filename)
{
    int fd = open(filename, O_RDWR | O_CREAT, S_IWUSR | S_IRUSR);

    if (fd == -1)
        exit(EXIT_FAILURE);

    off_t filesize = lseek(fd, 0, SEEK_END);
    Pager *pager = malloc(sizeof(Pager));
    pager->file_descriptor = fd;
    pager->file_length = filesize; // byte offset

    for (int i = 0; i < MAX_TABLE_PAGES; i++)
        pager->pages[i] = NULL;

    pager->pages[1] = calloc(1, PAGE_SIZE);
    PageHeader page_header;
    page_header.type = PAGE_LEAF;
    memcpy(pager->pages[1], &page_header, sizeof(PageHeader));

    LeafNode leaf_node_root;
    leaf_node_root.num_keys = 0;
    leaf_node_root.next_leaf = 0; // null page

    memcpy((char *)(pager->pages[1]) + sizeof(PageHeader), &leaf_node_root, sizeof(LeafNode));

    pager->pages[2] = calloc(1, PAGE_SIZE);
    PageHeader page_header2;
    page_header2.type = PAGE_HEAP;
    memcpy(pager->pages[2], &page_header2, sizeof(PageHeader));

    return pager;
}

int main(int argc, char *argv[])
{
    // DB_OPEN

    if (argc < 2)
    {
        printf("specify database filename\n");
        exit(EXIT_FAILURE);
    }
    char *fileame = argv[1];

    InputBuffer *input_buffer = createInputBuffer();
    Table *table = (Table *)malloc(sizeof(Table));

    Pager *pager = open_pager(fileame);

    table->num_rows = pager->file_length / ROW_SIZE;
    table->pager = pager;
    table->latest_heap_row = 0;
    table->latest_heap_page = 2;
    table->latest_page = 2;
    // latest node page is 1
    table->root = 1;
    // DB_OPEN

    while (1)
    {
        printf("db > ");
        getInput(input_buffer);

        // METACOMMANDS
        if (input_buffer->buffer[0] == '.')
        {
            int result = execute_meta_command(input_buffer->buffer, table);
            if (result == META_COMMAND_SUCCESS)
                continue;
            else if (result == META_COMMAND_UNRECOGNIZED)
            {
                printf("BAD COMMAND\n");
                continue;
            }
        }

        // DB COMMANDS
        Statement statement;
        int preparationStatus = prepareCommand(input_buffer, &statement);
        if (preparationStatus == PREPARATION_FAILURE)
        {
            printf("UNRECONIZED COMMAND\n");
            continue;
        }
        else if (preparationStatus == PREPARATION_STRING_OVERFLOW)
        {
            printf("STRING OVERFLOW\n");
            continue;
        }
        else if (preparationStatus == PREPARATION_SYNTAX_ERROR)
        {
            printf("SYNTAX ERROR\n");
            continue;
        }
        else if (preparationStatus == PREPARATION_NEGATIVE_ID)
        {
            printf("NEGATIVE ID\n");
            continue;
        }

        execute_statement(&statement, table);
        printf("Executed successfully\n");
    }

    db_close(table);
}

int execute_meta_command(char *buffer, Table *table)
{
    if (strcmp(buffer, ".exit") == 0)
    {
        exit(0);
        db_close(table);
    } // more else ifs to come...
    else
    {
        return META_COMMAND_UNRECOGNIZED;
    }
}

PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement)
{
    if (strncmp(input_buffer->buffer, "select", 6) == 0)
    {
        statement->type = STATEMENT_SELECT;
        return PREPARE_SUCCESS;
    }
    else if (strncmp(input_buffer->buffer, "insert", 6) == 0)
    {
        statement->type = STATEMENT_INSERT;

        char *keyword = strtok(input_buffer->buffer, " ");
        char *id_string = strtok(NULL, " ");
        char *username = strtok(NULL, " ");
        char *email = strtok(NULL, " ");

        if (id_string == NULL || username == NULL || email == NULL)
            return PREPARATION_SYNTAX_ERROR;

        int id = atoi(id_string);

        if (id < 0)
            return PREPARATION_NEGATIVE_ID;
        if (strlen(username) > COLUMN_USERNAME_SIZE)
            return PREPARATION_STRING_OVERFLOW;
        if (strlen(email) > COLUMN_EMAIL_SIZE)
            return PREPARATION_STRING_OVERFLOW;

        statement->row_to_insert.id = id;
        strcpy(statement->row_to_insert.username, username);
        strcpy(statement->row_to_insert.email, email);

        return PREPARE_SUCCESS;
    }
    if (strncmp(input_buffer->buffer, "ib", 2) == 0)
    {
        statement->type = STATEMENT_INSERT_BULK;
        return PREPARE_SUCCESS;
    }
    return PREPARATION_FAILURE;
}

ExecuteResult execute_statement(Statement *statement, Table *table)
{
    if (statement->type == STATEMENT_SELECT)
        return execute_select(statement, table);
    else if (statement->type == STATEMENT_INSERT)
        return execute_insert(statement, table);
    else if (statement->type == STATEMENT_INSERT_BULK)
    {
        for (int i = 0; i < 3000; i++)
        {
            Row row;
            row.id = i + 1;

            char buffer[6];
            char buffer2[16];
            for (int j = 0; j < 5; j++)
            {
                buffer[j] = 'a' + (rand() % 26);  // random lowercase letter
                buffer2[j] = 'a' + (rand() % 26); // random lowercase letter
            }
            buffer[5] = '\0';

            buffer2[5] = '@';
            buffer2[6] = 'g';
            buffer2[7] = 'm';
            buffer2[8] = 'a';
            buffer2[9] = 'i';
            buffer2[10] = 'l';
            buffer2[11] = '.';
            buffer2[12] = 'c';
            buffer2[13] = 'o';
            buffer2[14] = 'm';
            buffer2[15] = '\0';

            strcpy(row.username, buffer);
            strcpy(row.email, buffer2);

            Statement stmt;
            stmt.type = STATEMENT_INSERT;
            stmt.row_to_insert = row;
            printf("inserting id %d\n", stmt.row_to_insert.id);
            execute_insert(&stmt, table);
        }
        return EXECUTE_SUCCESS;
    }

    return EXECUTE_TABLE_FULL;
}

PageHeader *get_page(Pager *pager, PageID page_num)
{
    if (page_num > MAX_TABLE_PAGES)
        exit(EXIT_FAILURE);

    PageHeader *page_header;
    if (pager->pages[page_num] == NULL)
    {
        // cache miss. our informatoin is not in memory. go and load from files
        void *page = calloc(1, PAGE_SIZE);
        uint32_t num_pages_avaliable_on_disk = pager->file_length / PAGE_SIZE;
        if (pager->file_length % PAGE_SIZE)
            num_pages_avaliable_on_disk += 1;

        if (page_num < num_pages_avaliable_on_disk) // go ahead and read bytes from disk into page
        {
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes == -1)
                exit(EXIT_FAILURE);
        }
        else
        {
            exit(EXIT_FAILURE);
        }
        // consider exit failing in an else...
        // make a seprate "newpage" func
        pager->pages[page_num] = page;
        page_header = (PageHeader *)(pager->pages[page_num]);
    }
    else
    {
        page_header = (PageHeader *)(pager->pages[page_num]);
    }
    return page_header;
}

RowSlot *get_row_slot(Pager *pager, uint32_t page_num, uint32_t row_num_offset)
{
    if (page_num > MAX_TABLE_PAGES)
        exit(EXIT_FAILURE);

    if (pager->pages[page_num] == NULL)
    {
        // cache miss. our informatoin is not in memory. go and load from files
        void *page = malloc(PAGE_SIZE);
        uint32_t num_pages_avaliable_on_disk = pager->file_length / PAGE_SIZE;
        if (pager->file_length % PAGE_SIZE)
            num_pages_avaliable_on_disk += 1;

        if (page_num < num_pages_avaliable_on_disk)
        {
            lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);
            ssize_t bytes = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes == -1)
                exit(EXIT_FAILURE);
        }
        else
        {
            // if new page created
            PageHeader page_header;
            page_header.type = PAGE_HEAP;
            memcpy(page, &page_header, sizeof(PageHeader));
        }
        pager->pages[page_num] = page;
    }

    uint32_t byteoffset = (row_num_offset * ROW_SIZE) + HEADER_SIZE;

    RowID row_id;
    row_id.page_id = page_num;
    row_id.page_byte_offset = byteoffset;

    RowSlot *row_slot = malloc(sizeof(RowSlot));
    row_slot->row_id = row_id;
    row_slot->row_loc = pager->pages[page_num] + byteoffset;

    return row_slot;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;

    Row *row_to_insert = &(statement->row_to_insert);
    RowSlot *row_slot = get_row_slot(table->pager, table->latest_heap_page, table->latest_heap_row);
    // write to heap page at 'row_loc' location
    writeRow(row_to_insert, row_slot->row_loc);

    table->num_rows++;
    table->latest_heap_row++;

    if (table->latest_heap_row >= ROWS_PER_PAGE)
    {
        table->latest_page++;
        table->latest_heap_page = table->latest_page;
        table->latest_heap_row = 0;

        // we need a new heap page.
        // if the last node page is ahead or at the same page after an attempted increment,
        // then every page after this one is a node. take the page just after the last node
    }

    // NODE INSERT

    InsertResult root_result = insert_key(table, table->root, statement->row_to_insert.id, &row_slot->row_id);

    if (root_result.split)
    {
        PageHeader new_root_header;
        new_root_header.type = PAGE_INTERNAL;

        InternalNode new_root;
        new_root.keys[0] = root_result.promoted_key;
        new_root.num_keys = 1;
        new_root.page_ids[0] = table->root; // old root. now a left
        new_root.page_ids[1] = root_result.new_right_node;

        PageID last_page = allocate_page_internal(table, &new_root_header, &new_root);

        table->root = last_page;
    }

    // NODE INSERT

    free(row_slot);
    return EXECUTE_SUCCESS;
}

PageID node_page_slot(InternalNode *node, Key key)
{
    bool found = false;
    PageID result;
    for (int i = 0; i < node->num_keys; i++)
    {
        if (key < node->keys[i])
        {
            result = node->page_ids[i];
            found = true;
            break;
        }
    }
    // if exited loop w/o finding, we must go and enter very last last page ref.
    if (!found)
    {
        result = node->page_ids[node->num_keys]; // get last page id. remember, n+1 child ptrs (page ids). so last one is (node->num_keys-1) + 1
    }
    return result;
}

InsertResult split_node_leaf(LeafNode *node, Key *leaf_keys, RowID *leaf_rows, Table *table, RowID *row_slot)
{
    // 1. Find middle index
    uint32_t mid_index = LEAF_NODE_MAX_KEYS / 2;
    // 2. Create new node

    PageHeader page_header;
    page_header.type = PAGE_LEAF;

    LeafNode leaf_node_right;

    uint32_t temp_arr_len = node->num_keys + 1;

    // 3. Move keys [mid...] to new node
    // 4. Move child pointers accordingly
    memcpy(leaf_node_right.keys, &leaf_keys[mid_index], (temp_arr_len - mid_index) * sizeof(Key));
    memcpy(leaf_node_right.row_ids, &leaf_rows[mid_index], (temp_arr_len - mid_index) * sizeof(RowID));
    leaf_node_right.num_keys = (temp_arr_len - mid_index);

    // next leaf
    leaf_node_right.next_leaf = node->next_leaf;

    PageID latest_page = allocate_page_leaf(table, &page_header, &leaf_node_right);
    node->next_leaf = latest_page;

    InsertResult result;
    result.split = true;
    result.promoted_key = leaf_node_right.keys[0];
    result.new_right_node = latest_page;

    // remove keys from left old node
    node->num_keys = (mid_index - 1) + 1;

    printf("NODE LEAF SPLIT");

    return result;
}

InsertResult split_node_internal(InternalNode *node, Key *keys, PageID *pages, Table *table, RowID *row_slot)
{
    uint32_t mid_index = INTERNAL_NODE_MAX_KEYS / 2;

    PageHeader page_header;
    page_header.type = PAGE_INTERNAL;

    InternalNode internal_node_right;

    memcpy(internal_node_right.keys, &keys[mid_index + 1], (node->num_keys - (mid_index + 1)) * sizeof(Key));
    memcpy(internal_node_right.page_ids, &pages[mid_index + 1], (node->num_keys + 1 - (mid_index + 1)) * sizeof(PageID));

    internal_node_right.num_keys = (node->num_keys - (mid_index + 1));

    // next leaf soon
    PageID last_page = allocate_page_internal(table, &page_header, &internal_node_right);

    InsertResult result;
    result.split = true;
    result.promoted_key = keys[mid_index];
    result.new_right_node = last_page;

    // remove keys from left old node
    node->num_keys = mid_index;

    return result;
}

InsertResult insert_key(Table *table, PageID root_leaf_page_id, Key key_to_insert, RowID *row_slot)
{
    PageHeader *root_node_header = get_page(table->pager, root_leaf_page_id);

    if (root_node_header->type == PAGE_LEAF)
    {

        printf(" %d. going to (at) page %d\n", key_to_insert, root_leaf_page_id);

        LeafNode *node = ((char *)root_node_header + sizeof(PageHeader));
        if (node->num_keys == LEAF_NODE_MAX_KEYS) // if full
        {
            printf("leaf full now");

            // 0. simulate temp array
            Key temp_leaf_keys[LEAF_NODE_MAX_KEYS + 1];
            RowID temp_leaf_rows[LEAF_NODE_MAX_KEYS + 1];

            uint32_t len_temp_key = node->num_keys;
            uint32_t len_temp_row = node->num_keys;

            memcpy(temp_leaf_keys, node->keys, node->num_keys * sizeof(Key));
            memcpy(temp_leaf_rows, node->row_ids, node->num_keys * sizeof(RowID));

            array_insert_key_row_pair(temp_leaf_keys, temp_leaf_rows, key_to_insert, *row_slot, &len_temp_key, &len_temp_row);

            return split_node_leaf(node, temp_leaf_keys, temp_leaf_rows, table, row_slot);
        }
        else
        {
            uint32_t len_temp_row = node->num_keys;
            array_insert_key_row_pair(node->keys, node->row_ids, key_to_insert, *row_slot, &node->num_keys, &len_temp_row); // so that numkeys does not increment twice

            InsertResult result;
            result.split = false;
            result.promoted_key = 0;
            result.new_right_node = 0;

            return result;
        }
    }

    else
    {

        InternalNode *node = ((char *)root_node_header + sizeof(PageHeader));
        PageID dest_page = node_page_slot(node, key_to_insert);

        printf(" %d. going to page %d\n", key_to_insert, dest_page);

        InsertResult child_result = insert_key(table, dest_page, key_to_insert, row_slot);

        if (child_result.split)
        {

            printf("ROOT SPLIT: old_root=%u, new_right=%u, promoted_key=%u\n",
                   table->root, child_result.new_right_node, child_result.promoted_key);

            // incorporate child_result right node into this current node. attempt to insert.
            // if you get a +1 overflow you may split
            uint32_t insert_index = array_insert_key(node->keys, child_result.promoted_key, &node->num_keys);
            memmove(&node->page_ids[insert_index + 2], &node->page_ids[insert_index + 1], (node->num_keys - (insert_index + 1)) * sizeof(PageID));

            // keep as node->num_keys in the memmove. it incremented in the step before.
            // thats the value you need now (it now the initial length of the pages arr that you need for the bounds of moving)

            memcpy(&node->page_ids[insert_index + 1], &child_result.new_right_node, sizeof(PageID));

            // k1 k2 k3
            // p1 p2 p3 p4
            // --------------------
            // p1 k1 p2 k2 p3 k3 p4

            // k1 k2 k# k3
            // p1 p2 p3 p4
            // --------------------
            // p1 k1 p2 k2 p3 k# k3 p4

            if (node->num_keys > INTERNAL_NODE_MAX_KEYS)
            {
                // full. split.

                Key temp_keys[INTERNAL_NODE_MAX_KEYS + 1];
                memcpy(temp_keys, node->keys, node->num_keys * sizeof(Key));

                PageID temp_pages[INTERNAL_NODE_MAX_KEYS + 2];
                memcpy(temp_pages, node->page_ids, (node->num_keys + 1) * sizeof(PageID));

                return split_node_internal(node, temp_keys, temp_pages, table, row_slot);
            }
            else
            {
                InsertResult insert_result;
                insert_result.split = false;
                insert_result.promoted_key = 0;
                insert_result.new_right_node = 0;
                return insert_result;
            }
        }

        InsertResult insert_result;
        insert_result.split = false;
        insert_result.promoted_key = 0;
        insert_result.new_right_node = 0;
        return insert_result;
    }
}

uint32_t array_insert_key_row_pair(Key *key_arr, RowID *row_arr, Key key_value, RowID row_value, uint32_t *key_arr_len, uint32_t *row_arr_len)
{

    uint32_t i = 0;
    uint32_t j = *key_arr_len;
    while (i < j)
    {
        uint32_t mid = i + (j - i) / 2;
        if (key_arr[mid] < key_value)
            i = mid + 1;
        else
            j = mid;
    }
    uint32_t slot = i;

    memmove(&key_arr[(slot + 1)], &key_arr[slot], (*key_arr_len - slot) * sizeof(Key));
    memcpy(&key_arr[slot], &key_value, sizeof(Key));

    memmove(&row_arr[(slot + 1)], &row_arr[slot], (*row_arr_len - slot) * sizeof(RowID));
    memcpy(&row_arr[slot], &row_value, sizeof(RowID));

    (*key_arr_len)++;
    (*row_arr_len)++;
    return slot;
}

uint32_t array_insert_key(Key *key_arr, Key key_value, uint32_t *key_arr_len)
{

    uint32_t i = 0;
    uint32_t j = *key_arr_len;
    while (i < j)
    {
        uint32_t mid = i + (j - i) / 2;
        if (key_arr[mid] < key_value)
            i = mid + 1;
        else
            j = mid;
    }
    uint32_t slot = i;

    memmove(&key_arr[(slot + 1)], &key_arr[slot], (*key_arr_len - slot) * sizeof(Key));
    memcpy(&key_arr[slot], &key_value, sizeof(Key));

    (*key_arr_len)++;
    return slot;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{

    Row temp;
    PageID root_page = table->root;
    PageHeader *rootheader = get_page(table->pager, table->root);

    if (rootheader->type == PAGE_LEAF)
    {
        LeafNode *root = ((char *)rootheader + sizeof(PageHeader));

        for (int i = 0; i < root->num_keys; i++)
        {
            RowSlot *row_slot = get_row_slot(table->pager, root->row_ids[i].page_id, (root->row_ids[i].page_byte_offset - sizeof(PageHeader)) / ROW_SIZE);
            readRow(row_slot->row_loc, &temp);

            printf("(%d, %s, %s)\n", temp.id, temp.username, temp.email);
        }
    }
    else
    {
        InternalNode *root = ((char *)rootheader + sizeof(PageHeader));
        PageID curr_page_id;

        while (rootheader->type == PAGE_INTERNAL)
        {
            PageID next_page = root->page_ids[0];
            rootheader = get_page(table->pager, next_page);

            curr_page_id = root->page_ids[0];

            root = ((char *)rootheader + sizeof(PageHeader));
        }

        LeafNode *leaf = ((char *)rootheader + sizeof(PageHeader));

        while (curr_page_id != 0)
        {
            rootheader = get_page(table->pager, curr_page_id);
            leaf = ((char *)rootheader + sizeof(PageHeader));

            for (int i = 0; i < leaf->num_keys; i++)
            {
                RowSlot *row_slot = get_row_slot(table->pager, leaf->row_ids[i].page_id, (leaf->row_ids[i].page_byte_offset - sizeof(PageHeader)) / ROW_SIZE);
                readRow(row_slot->row_loc, &temp);

                printf("(%d, %s, %s)\n", temp.id, temp.username, temp.email);
            }
            curr_page_id = leaf->next_leaf;
        }
    }

    return EXECUTE_SUCCESS;
}

uint32_t allocate_page_leaf(Table *table, PageHeader *page_header, LeafNode *node)
{
    uint32_t last_page = table->latest_page;
    void *new_page = calloc(1, PAGE_SIZE);
    memcpy(new_page, page_header, sizeof(PageHeader));
    memcpy(new_page + sizeof(PageHeader), node, sizeof(LeafNode));
    table->pager->pages[last_page + 1] = new_page;
    table->latest_page++;

    return table->latest_page;
}

uint32_t allocate_page_internal(Table *table, PageHeader *page_header, InternalNode *node)
{
    uint32_t last_page = table->latest_page;
    void *new_page = calloc(1, PAGE_SIZE);
    memcpy(new_page, page_header, sizeof(PageHeader));
    memcpy(new_page + sizeof(PageHeader), node, sizeof(InternalNode));
    table->pager->pages[last_page + 1] = new_page;
    table->latest_page++;

    return table->latest_page;
}

void db_close(Table *table)
{
    // TODO: FIX AND FLUSH FOR ALL INITLIAZED PAGES. DONT DEPEND ON ROW COUNT
    // THIS ALSO ASSUMES ALL PAGES ARE HEAP. FIX THIS

    Pager *pager = table->pager;
    uint32_t number_full_pages = table->num_rows / ROWS_PER_PAGE;
    for (uint32_t i = 0; i < number_full_pages; i++)
    {
        if (pager->pages[i] != NULL)
        {
            pager_flush(pager, i, PAGE_SIZE);
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    uint32_t remainder_rows = table->num_rows % ROWS_PER_PAGE;
    if (remainder_rows > 0)
    {
        if (pager->pages[number_full_pages] != NULL)
        {
            pager_flush(pager, number_full_pages, remainder_rows * ROW_SIZE);
            free(pager->pages[number_full_pages]);
            pager->pages[number_full_pages] = NULL;
        }
    }

    int result = close(pager->file_descriptor);
    if (result == -1)
        exit(EXIT_FAILURE);

    for (uint32_t i = 0; i < MAX_TABLE_PAGES; i++)
    {
        if (pager->pages[i])
        {
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }
    free(pager);
    free(table);
}

void pager_flush(Pager *pager, uint32_t page_num, uint32_t size)
{
    if (pager->pages[page_num] == NULL)
        exit(EXIT_FAILURE);

    off_t offset = lseek(pager->file_descriptor, page_num * PAGE_SIZE, SEEK_SET);

    if (offset == -1)
        exit(EXIT_FAILURE);

    ssize_t bytes_written = write(pager->file_descriptor, pager->pages[page_num], size);
    if (bytes_written == -1)
        exit(EXIT_FAILURE);
}
