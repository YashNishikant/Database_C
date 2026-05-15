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

SearchResult get_immediate_key(Table *table, InternalNode *starting_node, uint32_t key_index_start)
{
    SearchResult result;

    InternalNode *node = starting_node;
    PageID curr_page = node->page_ids[key_index_start + 1];
    printf("1 get_page page_id=%u\n", (unsigned)curr_page);
    PageHeader *root_header = get_page(table->pager, curr_page);

    while (root_header->type == PAGE_INTERNAL)
    {
        node = process_page_header(root_header);
        printf("2 get_page page_id=%u\n", (unsigned)node->page_ids[0]);
        root_header = get_page(table->pager, node->page_ids[0]); // get header of this page id that you will go to
    }

    // now its a leaf...
    LeafNode *end_node = process_page_header(root_header);
    Key k = end_node->keys[0];
    result.exists = true;
    result.key = k;

    return result;
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;

    Row *target_row = &(statement->target_row);
    RowSlot row_slot = get_row_slot(table->pager, table->latest_heap_page, table->latest_heap_row);
    // write to heap page at 'row_loc' location
    writeRow(target_row, row_slot.row_loc);

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

    InsertResult root_result = insert_key(table, table->root, statement->target_row.id, row_slot.row_id);

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

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_delete(Statement *statement, Table *table)
{
    if (table->num_rows == 0)
        return EXECUTE_TABLE_EMPTY;

    printf("deleting id %d\n", statement->target_row.id);

    Row *row_to_delete = &(statement->target_row);

    // find key to delete. therefore you also find the row loc to target in pager

    RowSlot row_slot = get_row_slot(table->pager, table->latest_heap_page, table->latest_heap_row);

    // NODE DELETE

    // printf("checking %d\n\n", table->root);
    // printf("rowid %d\n\n", statement->target_row.id);
    DeleteResult root_result = delete_key(table, table->root, statement->target_row.id);

    if (root_result.DNE)
    {
        printf("Warning: Key Does Not Exist\n\n");
        return EXECUTE_SUCCESS;
    }

    if (root_result.underfilled)
    {
        printf("3 get_page page_id=%u\n", (unsigned)1);
        PageHeader *root_header = get_page(table->pager, 1);
        if (root_header->type == PAGE_INTERNAL)
        {
            InternalNode *root_node = process_page_header(root_header);
            if (root_node->num_keys == 0)
                table->root = root_node->page_ids[0];
        }
    }
    table->num_rows--;

    // NODE DELETE

    return EXECUTE_SUCCESS;
}

DeleteResult delete_key(Table *table, PageID root_leaf_page_id, Key key_to_delete)
{
    DeleteResult result;
    result.underfilled = false;
    result.manipulate_leaf = true;

    // printf("checking %d\n\n", root_leaf_page_id);
    printf("4 get_page page_id=%u\n", (unsigned)root_leaf_page_id);
    PageHeader *header = get_page(table->pager, root_leaf_page_id);
    if (header->type == PAGE_LEAF)
    {
        LeafNode *node = process_page_header(header);

        Key *key_ptr = bsearch(&key_to_delete, node->keys, node->num_keys, sizeof(Key), key_comparator);
        int *key_ind = key_ptr - node->keys; // returns the difference in units of the associated type, not bytes

        if (key_ptr == NULL)
            result.DNE = true;
        else
            result.DNE = false;
        return result;
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

        if (child_result.manipulate_leaf)
        {
            PageID leaf_page_id_mid = curr_node->page_ids[dest_ind];
            printf("5 get_page page_id=%u\n", (unsigned)leaf_page_id_mid);
            PageHeader *header = get_page(table->pager, leaf_page_id_mid);
            LeafNode *leaf_mid = process_page_header(header);

            Key *key_ptr = bsearch(&key_to_delete, leaf_mid->keys, leaf_mid->num_keys, sizeof(Key), key_comparator);
            int *key_ind = key_ptr - leaf_mid->keys; // returns the difference in units of the associated type, not bytes

            remove_key_row_leaf(leaf_mid, key_ind);

            if (leaf_mid->num_keys < MIN_KEYS_LEAF)
            {
                LeafNode *node_sib = NULL;
                LeafNode *node_merge_dest = NULL;
                PageID page_id_merge_dest = 0;
                int seperator_key_index = -1;
                int node_sib_key_ind = -1;
                bool merge = false;

                if (dest_ind > 0 && dest_ind < (curr_node->num_keys + 1) - 1)
                {
                    PageID leaf_page_id_right = curr_node->page_ids[dest_ind + 1];
                    PageID leaf_page_id_left = curr_node->page_ids[dest_ind - 1];

                    printf("6 get_page page_id=%u\n", (unsigned)leaf_page_id_right);
                    PageHeader *header_right = get_page(table->pager, leaf_page_id_right);
                    printf("7 get_page page_id=%u\n", (unsigned)leaf_page_id_left);
                    PageHeader *header_left = get_page(table->pager, leaf_page_id_left);

                    LeafNode *node_right = process_page_header(header_right);
                    LeafNode *node_left = process_page_header(header_left);

                    if (node_left->num_keys > MIN_KEYS_LEAF) // left can give
                    {
                        seperator_key_index = dest_ind - 1;
                        node_sib = node_left;
                        node_sib_key_ind = node_sib->num_keys - 1;
                    }
                    else if (node_right->num_keys > MIN_KEYS_LEAF) // right can give
                    {
                        seperator_key_index = dest_ind;
                        node_sib = node_right;
                        node_sib_key_ind = 0;
                    }
                    else // merge. neither can give
                    {
                        seperator_key_index = dest_ind - 1;
                        node_merge_dest = node_left;
                        page_id_merge_dest = leaf_page_id_left;
                        merge = true;
                    }
                }
                else if (dest_ind == 0)
                {
                    PageID page_id_right = curr_node->page_ids[dest_ind + 1];
                    printf("8 get_page page_id=%u\n", (unsigned)page_id_right);
                    PageHeader *header_right = get_page(table->pager, page_id_right);
                    InternalNode *node_right = process_page_header(header_right);

                    if (node_right->num_keys > MIN_KEYS_LEAF)
                    {
                        seperator_key_index = dest_ind;
                        node_sib = node_right;
                        node_sib_key_ind = 0;
                    }
                    else
                    {
                        seperator_key_index = dest_ind;
                        node_merge_dest = leaf_mid;
                        page_id_merge_dest = leaf_page_id_mid;
                        merge = true;
                    }
                }
                else if (dest_ind == curr_node->num_keys)
                {
                    PageID page_id_left = curr_node->page_ids[dest_ind - 1];
                    printf("9 get_page page_id=%u\n", (unsigned)page_id_left);
                    PageHeader *header_left = get_page(table->pager, page_id_left);
                    InternalNode *node_left = process_page_header(header_left);

                    if (node_left->num_keys > MIN_KEYS_LEAF) // left can give
                    {
                        seperator_key_index = dest_ind - 1;
                        node_sib = node_left;
                        node_sib_key_ind = node_sib->num_keys - 1;
                    }
                    else // merge. neither can give
                    {
                        seperator_key_index = dest_ind - 1;
                        node_merge_dest = node_left;
                        page_id_merge_dest = page_id_left;
                        merge = true;
                    }
                }

                if (node_merge_dest == NULL) // if merge == false
                {

                    array_insert_key_row_pair(leaf_mid->keys, leaf_mid->row_ids, node_sib->keys[node_sib_key_ind], node_sib->row_ids[node_sib_key_ind], &leaf_mid->num_keys);
                    remove_key_row_leaf(node_sib, node_sib_key_ind);

                    // parent separator to always be first key of the rightmost
                    printf("10 get_page page_id=%u\n", (unsigned)curr_node->page_ids[seperator_key_index + 1]);
                    PageHeader *rightmost_page_header = get_page(table->pager, curr_node->page_ids[seperator_key_index + 1]);
                    LeafNode *rightmost_leaf = process_page_header(rightmost_page_header);
                    curr_node->keys[seperator_key_index] = rightmost_leaf->keys[0];

                    DeleteResult result;
                    result.DNE = false;
                    result.manipulate_leaf = false;
                    result.underfilled = false;
                    return result;
                }
                else
                {
                    // neither can give...
                    // left most node will get the combination
                    // first move all keys from the original underfilled node, node_mid, to node_left

                    for (int i = leaf_mid->num_keys - 1; i >= 0; i--)
                    {
                        array_insert_key_row_pair(node_merge_dest->keys, node_merge_dest->row_ids, leaf_mid->keys[i], leaf_mid->row_ids[i], &node_merge_dest->num_keys);
                        remove_key_row_leaf(leaf_mid, i);
                    }

                    // remove parent separator key after merging two leaf children
                    Key k_temp;
                    RowID r_temp;
                    printf("1, separator_key_ind %d\n", seperator_key_index);
                    remove_key_page_internal(curr_node, seperator_key_index, &k_temp, &r_temp);
                    curr_node->page_ids[seperator_key_index] = page_id_merge_dest;

                    node_merge_dest->next_leaf = leaf_mid->next_leaf;

                    DeleteResult result;
                    result.DNE = false;
                    result.underfilled = false;
                    result.manipulate_leaf = false;
                    if (curr_node->num_keys < MIN_KEYS_INTERNAL)
                        result.underfilled = true;
                    return result;
                }
            }
        }

        else if (child_result.underfilled) // else if because we cannot be manipulating a leaf (starting deletion) with a node already underfilled.
        {

            InternalNode *node_sib = NULL;
            InternalNode *node_merge_dest = NULL;
            PageID page_id_merge_dest = 0;
            bool merge = false;
            PageID page_id_mid = curr_node->page_ids[dest_ind];
            printf("11 get_page page_id=%u\n", (unsigned)page_id_mid);
            PageHeader *header_mid = get_page(table->pager, page_id_mid);
            InternalNode *node_mid = process_page_header(header_mid);
            int seperator_key_index = -1;
            int node_sib_key_ind = -1;

            if (dest_ind > 0 && dest_ind < (curr_node->num_keys + 1) - 1)
            {
                PageID page_id_right = curr_node->page_ids[dest_ind + 1];
                PageID page_id_left = curr_node->page_ids[dest_ind - 1];

                printf("12 get_page page_id=%u\n", (unsigned)page_id_right);
                PageHeader *header_right = get_page(table->pager, page_id_right);
                printf("13 get_page page_id=%u\n", (unsigned)page_id_left);
                PageHeader *header_left = get_page(table->pager, page_id_left);

                InternalNode *node_right = process_page_header(header_right);
                InternalNode *node_left = process_page_header(header_left);

                if (node_left->num_keys > MIN_KEYS_INTERNAL) // left can give
                {
                    seperator_key_index = dest_ind - 1;
                    node_sib = node_left;
                    node_sib_key_ind = node_sib->num_keys - 1;
                }
                else if (node_right->num_keys > MIN_KEYS_INTERNAL) // right can give
                {
                    seperator_key_index = dest_ind;
                    node_sib = node_right;
                    node_sib_key_ind = 0;
                }
                else // merge. neither can give
                {
                    seperator_key_index = dest_ind - 1;
                    node_merge_dest = node_left;
                    page_id_merge_dest = page_id_left;
                    merge = true;
                }
            }
            else if (dest_ind == 0)
            {
                PageID page_id_right = curr_node->page_ids[dest_ind + 1];
                printf("14 get_page page_id=%u\n", (unsigned)page_id_right);
                PageHeader *header_right = get_page(table->pager, page_id_right);
                InternalNode *node_right = process_page_header(header_right);

                if (node_right->num_keys > MIN_KEYS_INTERNAL)
                {
                    seperator_key_index = dest_ind;
                    node_sib = node_right;
                    node_sib_key_ind = 0;
                }
                else
                {
                    seperator_key_index = dest_ind;
                    node_merge_dest = node_mid;
                    page_id_merge_dest = page_id_mid;
                    merge = true;
                }
            }
            else if (dest_ind == curr_node->num_keys)
            {
                PageID page_id_left = curr_node->page_ids[dest_ind - 1];
                printf("15 get_page page_id=%u\n", (unsigned)page_id_left);
                PageHeader *header_left = get_page(table->pager, page_id_left);
                InternalNode *node_left = process_page_header(header_left);

                if (node_left->num_keys > MIN_KEYS_INTERNAL) // left can give
                {
                    seperator_key_index = dest_ind - 1;
                    node_sib = node_left;
                    node_sib_key_ind = node_sib->num_keys - 1;
                }
                else // merge. neither can give
                {
                    seperator_key_index = dest_ind - 1;
                    node_merge_dest = node_left;
                    page_id_merge_dest = page_id_left;
                    merge = true;
                }
            }

            if (node_merge_dest == NULL) // if merge == false
            {
                PageID removed_page;
                Key removed_key;

                printf("2, separator_key_ind %d\n", seperator_key_index);
                remove_key_page_internal(curr_node->keys, seperator_key_index, &removed_key, &removed_page);
                array_insert_key_page_pair(node_mid->keys, node_mid->page_ids, &removed_key, &removed_page, &node_mid->num_keys);

                printf("3, separator_key_ind %d\n", seperator_key_index);
                remove_key_page_internal(node_sib->keys, node_sib_key_ind, &removed_key, &removed_page);
                array_insert_key_page_pair(curr_node->keys, curr_node->page_ids, &removed_key, &removed_page, &curr_node->num_keys);

                // take care of parent separator
                SearchResult search_res = get_immediate_key(table, curr_node, seperator_key_index);
                curr_node->keys[seperator_key_index] = search_res.key;

                DeleteResult result;
                result.DNE = false;
                result.manipulate_leaf = false;
                result.underfilled = false;
                return result;
            }
            else
            {
                // neither can give...
                // left most node will get the combination
                // first move all keys from the original underfilled node, node_mid, to node_left

                seperator_key_index = dest_ind - 1;

                for (int i = node_mid->num_keys; i >= 0; i--)
                {
                    PageID removed_page;
                    Key removed_key;

                    printf("4, i %d\n", i);
                    remove_key_page_internal(node_mid->keys, i, &removed_key, &removed_page);
                    array_insert_key_page_pair(node_merge_dest->keys, node_merge_dest->page_ids, &removed_key, &removed_page, &node_merge_dest->num_keys);
                }

                PageID removed_page;
                Key removed_key;

                printf("5, seperator_key_index %d\n", seperator_key_index);
                remove_key_page_internal(curr_node->keys, seperator_key_index, &removed_key, &removed_page);
                array_insert_key_page_pair(node_merge_dest->keys, node_merge_dest->page_ids, &removed_key, &removed_page, &node_merge_dest->num_keys);

                curr_node->page_ids[seperator_key_index] = page_id_merge_dest;

                DeleteResult result;
                result.DNE = false;
                result.underfilled = false;
                result.manipulate_leaf = false;
                if (curr_node->num_keys < MIN_KEYS_INTERNAL)
                    result.underfilled = true;
                return result;
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

InsertResult split_node_leaf(LeafNode *node, Key *leaf_keys, RowID *leaf_rows, Table *table)
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

InsertResult split_node_internal(InternalNode *node, Key *keys, PageID *pages, Table *table)
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

InsertResult insert_key(Table *table, PageID root_leaf_page_id, Key key_to_insert, RowID row_slot)
{
    printf("16 get_page page_id=%u\n", (unsigned)root_leaf_page_id);
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

            memcpy(temp_leaf_keys, node->keys, node->num_keys * sizeof(Key));
            memcpy(temp_leaf_rows, node->row_ids, node->num_keys * sizeof(RowID));

            array_insert_key_row_pair(temp_leaf_keys, temp_leaf_rows, key_to_insert, row_slot, &len_temp_key);

            return split_node_leaf(node, temp_leaf_keys, temp_leaf_rows, table);
        }
        else
        {
            array_insert_key_row_pair(node->keys, node->row_ids, key_to_insert, row_slot, &node->num_keys);

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

                return split_node_internal(node, temp_keys, temp_pages, table);
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

void array_insert_key_row_pair(Key *key_arr, RowID *row_arr, Key key_value, RowID row_value, uint32_t *arr_len)
{

    uint32_t i = 0;
    uint32_t j = *arr_len;
    while (i < j)
    {
        uint32_t mid = i + (j - i) / 2;
        if (key_arr[mid] < key_value)
            i = mid + 1;
        else
            j = mid;
    }
    uint32_t slot = i;

    memmove(&key_arr[(slot + 1)], &key_arr[slot], (*arr_len - slot) * sizeof(Key));
    memcpy(&key_arr[slot], &key_value, sizeof(Key));

    memmove(&row_arr[(slot + 1)], &row_arr[slot], (*arr_len - slot) * sizeof(RowID));
    memcpy(&row_arr[slot], &row_value, sizeof(RowID));

    (*arr_len)++;
}

void array_insert_key_page_pair(Key *key_arr, PageID *page_id_arr, Key key_value, PageID to_insert, uint32_t *key_arr_len)
{

    if (to_insert == 0)
    {
        printf("here");
        exit(EXIT_FAILURE);
    }

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

void remove_key_row_leaf(LeafNode *node, int index)
{
    memmove(&node->keys[index], &node->keys[index + 1], (node->num_keys - index - 1) * sizeof(Key));
    memmove(&node->row_ids[index], &node->row_ids[index + 1], (node->num_keys - index - 1) * sizeof(RowID));
    node->num_keys--;
}

void remove_key_page_internal(InternalNode *node, int index, Key *key_removed, PageID *page_id_removed)
{
    *key_removed = node->keys[index];
    *page_id_removed = node->page_ids[index + 1];
    memmove(&node->keys[index], &node->keys[index + 1], (node->num_keys - index) * sizeof(Key));
    memmove(&node->page_ids[index + 1], &node->page_ids[index + 2], ((node->num_keys + 1) - (index + 1)) * sizeof(PageID));
    node->num_keys--;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{

    Row temp;
    PageID root_page = table->root;
    printf("17 get_page page_id=%u\n", (unsigned)table->root);
    PageHeader *rootheader = get_page(table->pager, table->root);

    if (rootheader->type == PAGE_LEAF)
    {
        LeafNode *root = ((char *)rootheader + sizeof(PageHeader));

        for (int i = 0; i < root->num_keys; i++)
        {
            RowSlot row_slot = get_row_slot(table->pager, root->row_ids[i].page_id, (root->row_ids[i].page_byte_offset - sizeof(PageHeader)) / ROW_SIZE);
            readRow(row_slot.row_loc, &temp);

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
            printf("18 get_page page_id=%u\n", (unsigned)next_page);
            rootheader = get_page(table->pager, next_page);

            curr_page_id = root->page_ids[0];

            root = ((char *)rootheader + sizeof(PageHeader));
        }

        LeafNode *leaf = ((char *)rootheader + sizeof(PageHeader));

        while (curr_page_id != 0)
        {
            printf("19 get_page page_id=%u\n", (unsigned)curr_page_id);
            rootheader = get_page(table->pager, curr_page_id);
            leaf = ((char *)rootheader + sizeof(PageHeader));

            for (int i = 0; i < leaf->num_keys; i++)
            {
                RowSlot row_slot = get_row_slot(table->pager, leaf->row_ids[i].page_id, (leaf->row_ids[i].page_byte_offset - sizeof(PageHeader)) / ROW_SIZE);
                readRow(row_slot.row_loc, &temp);

                printf("(%d, %s, %s)\n", temp.id, temp.username, temp.email);
            }
            curr_page_id = leaf->next_leaf;
        }
    }

    return EXECUTE_SUCCESS;
}
