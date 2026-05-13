#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include <stdint.h>
#include "types.h"
#include "b_tree.h"

void *process_page_header(PageHeader *header)
{
    return ((char *)header + sizeof(PageHeader));
}

int key_comparator(const void *a, const void *b)
{
    int val_a = *(Key *)a;
    int val_b = *(Key *)b;
    if (val_a < val_b)
        return -1;
    if (val_a > val_b)
        return 1;
    return 0;
}

// SearchResult search(Table *table, Key target)
// {
//     PageHeader *root_header = get_page(table->pager, table->root);
//     PageID curr_page = table->root;

//     SearchResult result;
//     result.InternalPage = 0;
//     result.InternalIndex = 0;
//     result.LeafPage = 0;
//     result.LeafIndex = 0;

//     while (root_header->type == PAGE_INTERNAL)
//     {
//         InternalNode *node = process_page_header(root_header);
//         int key_result_ind = bsearch(target, node->keys, node->num_keys, sizeof(Key), compare_ints);
//         if (key_result_ind != -1)
//         {
//             // we've found the key in an internal node
//             result.InternalPage = curr_page;
//             result.InternalIndex = key_result_ind;
//             curr_page = node->page_ids[key_result_ind + 1];
//         }
//         else
//             curr_page = node_page_slot(table, target); // find where to go down the tree

//         PageHeader *root_header = get_page(table->pager, curr_page); // get header of this page id that you will go to
//     }
//     // now its a leaf...
//     LeafNode *node = process_page_header(root_header);
//     int key_result_ind = bsearch(target, node->keys, node->num_keys, sizeof(Key), compare_ints);
//     if (key_result_ind != -1)
//     {
//         // we've found the key in a leaf --> this is the data entry
//         result.LeafPage = curr_page;
//         result.LeafIndex = key_result_ind;
//         result.Row_ID = node->row_ids[key_result_ind];
//     }
//     return result;
// }

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;

    Row *target_row = &(statement->target_row);
    RowSlot *row_slot = get_row_slot(table->pager, table->latest_heap_page, table->latest_heap_row);
    // write to heap page at 'row_loc' location
    writeRow(target_row, row_slot->row_loc);

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

    InsertResult root_result = insert_key(table, table->root, statement->target_row.id, &row_slot->row_id);

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

ExecuteResult execute_delete(Statement *statement, Table *table)
{
    if (table->num_rows == 0)
        return EXECUTE_TABLE_EMPTY;

    Row *row_to_delete = &(statement->target_row);

    // find key to delete. therefore you also find the row loc to target in pager

    RowSlot *row_slot = get_row_slot(table->pager, table->latest_heap_page, table->latest_heap_row);
    // SearchResult search_result = search(table, statement->target_row.id);

    // if (search_result.InternalPage != 0)
    // {
    //     PageHeader *internal_to_remove_header = get_page(table, search_result.InternalPage);
    //     InternalNode *internal_to_remove = process_page_header(internal_to_remove_header);
    // }
    // if (search_result.LeafPage != 0)
    // {
    //     PageHeader *leaf_to_remove_header = get_page(table, search_result.LeafPage);
    //     LeafNode *leaf_to_remove = process_page_header(leaf_to_remove_header);

    //     // TODO:
    //     // search_result.Row_ID; --> add to some type of removal list...
    // }

    table->num_rows--;

    // NODE DELETE

    // InsertResult root_result = delete_key(table, table->root, statement->target_row.id);

    // if (root_result.split)
    // {
    //     PageHeader new_root_header;
    //     new_root_header.type = PAGE_INTERNAL;

    //     InternalNode new_root;
    //     new_root.keys[0] = root_result.promoted_key;
    //     new_root.num_keys = 1;
    //     new_root.page_ids[0] = table->root; // old root. now a left
    //     new_root.page_ids[1] = root_result.new_right_node;

    //     PageID last_page = allocate_page_internal(table, &new_root_header, &new_root);

    //     table->root = last_page;
    // }

    // NODE INSERT

    free(row_slot);
    return EXECUTE_SUCCESS;
}

DeleteResult delete_key(Table *table, PageID root_leaf_page_id, Key key_to_delete)
{

    PageHeader *header = get_page(table, root_leaf_page_id);
    if (header->type == PAGE_LEAF)
    {
        LeafNode *node = process_page_header(header);
        int index = bsearch(key_to_delete, node->keys, node->num_keys, sizeof(Key), key_comparator);
        if (index != -1)
        {
            // fail. not there
            DeleteResult result;
            result.DNE = true;
            return result;
        }
        else
        {
            // if what we are looking for is in the leaf node...
            // we can either safely remove it... or we need to borrow an elem from a sib

            remove_key_leaf(node, index);

            if (node->num_keys < MIN_KEYS_LEAF) // solo deletion
            {
                // borrowing / sibling management needs to be done in the parent. mark what needs to be done in the result struct
                DeleteResult result;
                result.underfilled = true;
                result.DNE = false;
            }
            else
            {
                DeleteResult result;
                result.underfilled = false;
                result.DNE = false;
            }
        }
    }
    else
    {
        InternalNode *curr_node = process_page_header(header);
        int dest_ind = node_page_slot(curr_node, key_to_delete);
        DeleteResult child_result = delete_key(table, curr_node->page_ids[dest_ind], key_to_delete);
        if (child_result.DNE)
        {
            DeleteResult result;
            result.DNE = true;
            return result;
        }
        if (child_result.underfilled)
        {
            if (dest_ind > 0 && dest_ind < curr_node->num_keys)
            {
                PageHeader *header_right = get_page(table, dest_ind + 1);
                PageHeader *header_mid = get_page(table, dest_ind);
                PageHeader *header_left = get_page(table, dest_ind - 1);
                if (header_right->type == PAGE_INTERNAL)
                {
                    InternalNode *node_right = process_page_header(header_right);
                    InternalNode *node_mid = process_page_header(header_mid);
                    InternalNode *node_left = process_page_header(header_left);

                    if (node_left->num_keys > MIN_KEYS_LEAF) // left can give. what populates the underfilled is the key between pages dest_ind and dest_ind-1
                    {
                        int seperator_key_index = dest_ind - 1;

                        PageID removed_page;
                        Key removed_key;
                        remove_key_internal(curr_node->keys, seperator_key_index, removed_key, removed_page);
                        array_insert_key_page_pair(node_mid->keys, node_mid->page_ids, &removed_key, &removed_page, node_mid->num_keys);

                        remove_key_internal(node_left->keys, node_left->num_keys - 1, removed_key, removed_page);
                        array_insert_key_page_pair(curr_node->keys, curr_node->page_ids, &removed_key, &removed_page, curr_node->num_keys);

                        DeleteResult result;
                        result.DNE = false;
                        result.underfilled = false;
                        return result;
                    }
                    else if (node_right->num_keys > MIN_KEYS_LEAF) // left cannot give... right can give
                    {
                        int seperator_key_index = dest_ind;

                        PageID removed_page;
                        Key removed_key;
                        remove_key_internal(curr_node->keys, seperator_key_index, removed_key, removed_page);
                        array_insert_key_page_pair(node_mid->keys, node_mid->page_ids, &removed_key, &removed_page, node_mid->num_keys);

                        remove_key_internal(node_right->keys, 0, removed_key, removed_page);
                        array_insert_key_page_pair(curr_node->keys, curr_node->page_ids, &removed_key, &removed_page, curr_node->num_keys);

                        DeleteResult result;
                        result.DNE = false;
                        result.underfilled = false;
                        return result;
                    }
                    else
                    { // neither can give...
                    }
                }
                else
                {
                    LeafNode *node_right = process_page_header(header_right);
                    LeafNode *node_left = process_page_header(header_left);
                    if (node_left->num_keys > MIN_KEYS_LEAF) // left can give
                    {
                    }
                    else if (node_right->num_keys > MIN_KEYS_LEAF) // left cannot give... right can give
                    {
                    }
                    else
                    { // neither can give...
                    }
                }
            }
            else if (dest_ind == 0)
            {
                PageHeader *header_right = get_page(table, dest_ind + 1);
                if (header_right->type == PAGE_INTERNAL)
                {
                    InternalNode *node_right = process_page_header(header_right);
                }
                else
                {
                    LeafNode *node_right = process_page_header(header_right);
                }
            }
            else if (dest_ind == curr_node->num_keys)
            {
                PageHeader *header_left = get_page(table, dest_ind - 1);
                if (header_left->type == PAGE_INTERNAL)
                {
                    InternalNode *node_left = process_page_header(header_left);
                }
                else
                {
                    LeafNode *node_left = process_page_header(header_left);
                }
            }
        }
    }
}

int node_page_slot(InternalNode *node, Key key)
{
    bool found = false;
    int result = -1;
    for (int i = 0; i < node->num_keys; i++)
    {
        if (key < node->keys[i])
        {
            result = i;
            found = true;
            break;
        }
    }
    // if exited loop w/o finding, we must go and enter very last last page ref.
    if (!found)
    {
        result = node->num_keys; // get last page id. remember, n+1 child ptrs (page ids). so last one is (node->num_keys-1) + 1
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

    // 3. Move keys [mid...] to new node (RIGHT NODE)
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
    // left node has all keys up until but not including mid.
    // num keys is still mid_index bc we include the 0 index
    node->num_keys = mid_index;

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
    // left node has all keys up until but not including mid.
    // num keys is still mid_index bc we include the 0 index
    node->num_keys = mid_index;

    return result;
}

InsertResult insert_key(Table *table, PageID root_leaf_page_id, Key key_to_insert, RowID *row_slot)
{
    PageHeader *root_node_header = get_page(table->pager, root_leaf_page_id);

    if (root_node_header->type == PAGE_LEAF)
    {

        LeafNode *node = ((char *)root_node_header + sizeof(PageHeader));
        if (node->num_keys == LEAF_NODE_MAX_KEYS) // if full
        {
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
        int dest_ind = node_page_slot(node, key_to_insert);

        InsertResult child_result = insert_key(table, node->page_ids[dest_ind], key_to_insert, row_slot);

        if (child_result.split)
        {

            // incorporate child_result right node into this current node. attempt to insert.
            // if you get a +1 overflow you may split

            array_insert_key_page_pair(node->keys, node->page_ids, child_result.promoted_key, child_result.new_right_node, &node->num_keys);

            // keep as node->num_keys in the memmove. it incremented in the step before.
            // thats the value you need now (it now the initial length of the pages arr that you need for the bounds of moving)

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

void array_insert_key_row_pair(Key *key_arr, RowID *row_arr, Key key_value, RowID row_value, uint32_t *key_arr_len, uint32_t *row_arr_len)
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
}

void array_insert_key_page_pair(Key *key_arr, PageID *page_id_arr, Key key_value, PageID to_insert, uint32_t *key_arr_len)
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

    memmove(&page_id_arr[slot + 2], &page_id_arr[slot + 1], ((*key_arr_len + 1) - (slot + 1)) * sizeof(PageID));
    memcpy(&page_id_arr[slot + 1], &to_insert, sizeof(PageID));
    (*key_arr_len)++;
}

void remove_key_leaf(LeafNode *node, int index)
{
    memmove(&node->keys[index], &node->keys[index + 1], (node->num_keys - index) * sizeof(Key));
    memmove(&node->row_ids[index], &node->row_ids[index + 1], (node->num_keys - index) * sizeof(Key));
    node->num_keys--;
}

void remove_key_internal(InternalNode *node, int index, Key *key_removed, PageID *page_id_removed)
{
    *key_removed = node->keys[index];
    *page_id_removed = node->page_ids[index + 1];
    memmove(&node->keys[index], &node->keys[index + 1], (node->num_keys - index) * sizeof(Key));
    memmove(&node->page_ids[index + 1], &node->page_ids[index + 2], ((node->num_keys + 1) - (index + 1)) * sizeof(Key));
    node->num_keys--;
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
