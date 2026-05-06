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
