#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdbool.h>
#include "b_tree.h"
#include "operations.h"

int main(int argc, char *argv[])
{
    // DB_OPEN

    if (argc < 2)
    {
        printf("EXIT FAILURE -> main(): db file not specified");
        exit(EXIT_FAILURE);
    }
    char *fileame = argv[1];

    InputBuffer *input_buffer = createInputBuffer();
    Table *table = (Table *)malloc(sizeof(Table));

    Pager *pager = init_pager_and_table(fileame, table);

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
