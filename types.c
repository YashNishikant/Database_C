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

//      key 0  |  key 1  |  key 2  |  key 3  |
// page 0 |  page 1 |  page 2 |  page 3 |  page 4 |

const uint32_t PAGE_SIZE = 4096; // 4kb ✅
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
const uint32_t MIN_KEYS_INTERNAL = INTERNAL_NODE_MAX_KEYS / 2;
const uint32_t MIN_KEYS_LEAF = LEAF_NODE_MAX_KEYS / 2;