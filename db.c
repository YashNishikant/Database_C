#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "types.h"

char *getInput(InputBuffer *input_buffer);
InputBuffer *createInputBuffer();
PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_statement(Statement *statement, Table *table);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);

void advance_cursor(Cursor *cursor)
{
    cursor->row_num_offset += 1;
    if (cursor->row_num_offset >= cursor->table->num_rows)
        cursor->end_of_table = true;
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

    pager->pages[0] = malloc(PAGE_SIZE);
    PageHeader page_header;
    page_header.type = PAGE_LEAF;
    memcpy(pager->pages[0], &page_header, sizeof(PageHeader));

    LeafNode leaf_node_root;
    for (int i = 0; i < LEAF_NODE_MAX_KEYS; i++)
    {
        leaf_node_root.keys[i] = 0;
        RowID row_id;
        row_id.page_id = 0;
        row_id.offset = 0;
        leaf_node_root.row_ids[i] = row_id;
    }
    memcpy((char *)(pager->pages[0]) + sizeof(PageHeader), &leaf_node_root, sizeof(LeafNode));

    pager->pages[1] = malloc(PAGE_SIZE);
    PageHeader page_header2;
    page_header2.type = PAGE_HEAP;
    memcpy(pager->pages[1], &page_header2, sizeof(PageHeader));

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
    table->latest_heap_page = 1;
    table->latest_node_page = 0;
    table->root = 0;
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
    return PREPARATION_FAILURE;
}

ExecuteResult execute_statement(Statement *statement, Table *table)
{
    if (statement->type == STATEMENT_SELECT)
        return execute_select(statement, table);
    else if (statement->type == STATEMENT_INSERT)
        return execute_insert(statement, table);

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
        void *page = malloc(PAGE_SIZE);
        __uint32_t num_pages_avaliable_on_disk = pager->file_length / PAGE_SIZE;
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

RowSlot *get_row_slot(Cursor *cursor)
{
    Pager *pager = cursor->table->pager;
    __uint32_t pagenum = cursor->page_num;

    if (pagenum > MAX_TABLE_PAGES)
        exit(EXIT_FAILURE);

    if (pager->pages[pagenum] == NULL)
    {
        // cache miss. our informatoin is not in memory. go and load from files
        void *page = malloc(PAGE_SIZE);
        __uint32_t numpagesavaliable = pager->file_length / PAGE_SIZE;
        if (pager->file_length % PAGE_SIZE)
            numpagesavaliable += 1;

        if (pagenum < numpagesavaliable)
        {
            lseek(pager->file_descriptor, pagenum * PAGE_SIZE, SEEK_SET);
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
        pager->pages[pagenum] = page;
    }

    __uint32_t byteoffset = (cursor->row_num_offset * ROW_SIZE) + HEADER_SIZE;

    RowID row_id;
    row_id.page_id = pagenum;
    row_id.offset = byteoffset;

    RowSlot *row_slot = malloc(sizeof(RowSlot));
    row_slot->row_id = row_id;
    row_slot->row_loc = pager->pages[pagenum] + byteoffset;

    return row_slot;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->table = table;
    cursor->page_num = table->latest_heap_page;
    cursor->row_num_offset = table->latest_heap_row;
    cursor->end_of_table = true;

    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;

    Row *row_to_insert = &(statement->row_to_insert);
    RowSlot *row_slot = get_row_slot(cursor);

    writeRow(row_to_insert, row_slot->row_loc);

    table->num_rows++;
    table->latest_heap_row++;
    if (table->latest_heap_row >= ROWS_PER_PAGE)
    {
        table->latest_heap_page++;
        // we need a new heap page.
        // if the last node page is ahead or at the same page after an attempted increment,
        // then every page after this one is a node. take the page just after the last node
        if (table->latest_node_page >= table->latest_heap_page)
            table->latest_heap_page = table->latest_node_page + 1;

        table->latest_heap_row = 0;
    }

    // NODE INSERT

    PageID root = table->root;
    PageHeader *root_page_header = get_page(table->pager, root);
    Key key_insert = statement->row_to_insert.id;

    PageID leaf_node_ind = find_leaf_node(root_page_header, key_insert, table, row_slot);

    // NODE INSERT

    free(row_slot);
    free(cursor);
    return EXECUTE_SUCCESS;
}

PageID get_last_page(Table *table)
{
    if (table->latest_heap_page > table->latest_node_page)
        return table->latest_heap_page;
    return table->latest_node_page;
}

PageID find_leaf_node(PageHeader *root_page_header, Key key, Table *table, RowID *row_slot)
{

    if (table->num_rows <= LEAF_NODE_MAX_KEYS) // only 1 node in tree -> leaf.
        return 0;                              // todo: fix this

    PageStack page_stack;
    page_stack.length = 0;
    PageID leaf_page_result;

    while (root_page_header->type == PAGE_INTERNAL)
    {
        // page0 key0 page1 key1 page2 key2 page3
        InternalNode *node = (InternalNode *)((char *)(root_page_header) + sizeof(PageHeader));
        bool found = false;
        for (int i = 0; i < node->num_keys; i++)
        {
            if (key < node->keys[i])
            {
                node->page_ids[i]; // this is the page to go to
                leaf_page_result = node->page_ids[i];
                root_page_header = get_page(table->pager, node->page_ids[i]);

                page_stack.array[i] = leaf_page_result;
                page_stack.length++;

                found = true;
            }
        }
        // if exited loop w/o finding, we must go and enter very last last page ref.
        if (!found)
        {
            root_page_header = get_page(table->pager, node->page_ids[node->num_keys]); // get last page id.
            leaf_page_result = node->page_ids[node->num_keys];

            page_stack.array[node->num_keys] = leaf_page_result;
            page_stack.length++;
        }
    }

    insert_key(table, root_page_header, key, row_slot, page_stack, leaf_page_result);
}

void array_insert(Key *keys_arr, RowID *row_arr, uint32_t arr_len, RowID *row_slot, Key key_to_insert)
{
    uint32_t slot = 0;
    uint32_t i = 0;
    uint32_t j = arr_len;
    while (i < j)
    {
        uint32_t mid = i + (j - i) / 2;
        if (keys_arr[mid] > key_to_insert)
            i = mid + 1;
        else
        {
            slot = mid;
            j = mid - 1;
        }
    }

    if (slot == LEAF_NODE_MAX_KEYS)
    { // last ind
        keys_arr[LEAF_NODE_MAX_KEYS] = key_to_insert;
        row_arr[LEAF_NODE_MAX_KEYS] = *row_slot;
    }
    else
    {
        memcpy(&keys_arr[slot + 1], &keys_arr[slot], (arr_len - slot) * sizeof(Key));
        keys_arr[slot] = key_to_insert;

        memcpy(&row_arr[slot + 1], &row_arr[slot], (arr_len - slot) * sizeof(RowID));
        row_arr[slot] = *row_slot;
    }
}

void insert_key(Table *table, LeafNode *node, Key key_to_insert, RowID *row_slot, PageStack page_stack, PageID root_leaf_page_id)
{
    if (node->num_keys == LEAF_NODE_MAX_KEYS)
    {

        // 0. simulate temp array
        Key temp_leaf_keys[LEAF_NODE_MAX_KEYS + 1];
        RowID temp_leaf_rows[LEAF_NODE_MAX_KEYS + 1];
        memcpy(temp_leaf_keys, node->keys, node->num_keys * sizeof(Key));
        memcpy(temp_leaf_rows, node->row_ids, node->num_keys * sizeof(RowID));

        array_insert(temp_leaf_keys, temp_leaf_rows, node->num_keys, row_slot, key_to_insert);

        // 1. Find middle index
        uint32_t mid_index = LEAF_NODE_MAX_KEYS / 2;
        // 2. Create new internal node
        LeafNode *new_leaf_node = malloc(sizeof(LeafNode));
        PageID last_page = get_last_page(table);

        // 3. Move keys [mid+1...] to new node
        PageHeader page_header;
        page_header.type = PAGE_LEAF;

        LeafNode leaf_node_right;

        // 4. Move child pointers accordingly
        memcpy(leaf_node_right.keys, &temp_leaf_keys[mid_index + 1], (node->num_keys - (mid_index + 1)) * sizeof(Key));
        memcpy(leaf_node_right.row_ids, &temp_leaf_rows[mid_index + 1], (node->num_keys - (mid_index + 1)) * sizeof(RowID));

        void *new_node_page_right = malloc(NODE_LEAF);
        memcpy(new_node_page_right, &page_header, sizeof(PageHeader));
        memcpy(new_node_page_right + sizeof(PageHeader), &leaf_node_right, sizeof(LeafNode));
        table->pager->pages[last_page + 1] = new_node_page_right;
        table->latest_node_page++;

        // 5. Promote node->keys[mid] to parent
        if (table->num_rows >= LEAF_NODE_MAX_KEYS)
        {
            // special case. 1 -> 3
            // create new root (parent)

            InternalNode new_root_node;
            new_root_node.page_ids[0] = root_leaf_page_id; // left child
            new_root_node.keys[0] = node->keys[mid_index]; // sandwich key in middle
            new_root_node.page_ids[1] = last_page + 1;     // right child -> leaf_node_right
            new_root_node.num_keys++;

            void *new_root_parent = malloc(PAGE_INTERNAL);
            memcpy(new_root_parent, &page_header, sizeof(PageHeader));
            memcpy(new_root_parent + sizeof(PageHeader), &new_root_node, sizeof(InternalNode));
            table->pager->pages[last_page + 1] = new_root_parent;
            table->latest_node_page++;
        }

        // 6. Insert the new key into appropriate half
        LeafNode *leaf_node_right_temp = ((char *)new_node_page_right + sizeof(PageHeader));
        LeafNode *leaf_node_left_temp = ((char *)node + sizeof(PageHeader));

        uint32_t len_right = leaf_node_right_temp->num_keys;

        if (key_to_insert > leaf_node_left_temp->keys[len_right - 1])
            array_insert(leaf_node_right_temp->keys, leaf_node_right_temp->row_ids, leaf_node_right_temp->num_keys, row_slot, key_to_insert);
        else if (key_to_insert < leaf_node_right_temp->keys[0])
            array_insert(leaf_node_left_temp->keys, leaf_node_left_temp->row_ids, leaf_node_left_temp->num_keys, row_slot, key_to_insert);

        // todo: work on propogating...
    }
    else
    {
        Key *keys_arr = node->keys;
        RowID *rowid_arr = node->row_ids;
        uint32_t slot = -1;
        uint32_t i = 0;
        uint32_t j = node->num_keys;

        while (i < j)
        {
            uint32_t mid = i + (j - i) / 2;
            if (keys_arr[mid] > key_to_insert)
                i = mid + 1;
            else
            {
                slot = mid;
                j = mid - 1;
            }
        }

        memcpy(&keys_arr[slot + 1], &keys_arr[slot], (node->num_keys - slot) * sizeof(Key));
        memcpy(&rowid_arr[slot + 1], &rowid_arr[slot], (node->num_keys - slot) * sizeof(RowID));

        keys_arr[slot] = key_to_insert;
        rowid_arr[slot] = *row_slot;
    }
}

ExecuteResult execute_select(Statement *statement, Table *table)
{
    Row temp;
    Cursor *cursor = malloc(sizeof(Cursor));
    cursor->end_of_table = false;
    cursor->table = table;
    cursor->page_num = 0;
    cursor->row_num_offset = 0;

    // while (!(cursor->end_of_table))
    // {
    //     __uint32_t pagenum = cursor->row_num / ROWS_PER_PAGE;
    //     void *page = table->pager->pages[pagenum];
    //     PageHeader *page_header = (PageHeader *)page;
    //     if (page_header->type == PAGE_HEAP)
    //     {
    //         RowSlot *row_slot = get_row_slot(cursor);

    //         readRow(row_slot->row_loc, &temp);
    //         printf("(%d, %s, %s )\n", temp.id, temp.username, temp.email);
    //     }
    //     advance_cursor(cursor);
    // }
    free(cursor);
    return EXECUTE_SUCCESS;
}

void db_close(Table *table)
{
    // TODO: FIX AND FLUSH FOR ALL INITLIAZED PAGES. DONT DEPEND ON ROW COUNT
    // THIS ALSO ASSUMES ALL PAGES ARE HEAP. FIX THIS

    Pager *pager = table->pager;
    __uint32_t number_full_pages = table->num_rows / ROWS_PER_PAGE;
    for (__uint32_t i = 0; i < number_full_pages; i++)
    {
        if (pager->pages[i] != NULL)
        {
            pager_flush(pager, i, PAGE_SIZE);
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    __uint32_t remainder_rows = table->num_rows % ROWS_PER_PAGE;
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

    for (__uint32_t i = 0; i < MAX_TABLE_PAGES; i++)
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

void pager_flush(Pager *pager, __uint32_t page_num, __uint32_t size)
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