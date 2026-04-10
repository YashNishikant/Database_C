#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#define COLUMN_USERNAME_SIZE 32
#define COLUMN_EMAIL_SIZE 255

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
    __uint32_t id;
    char username[COLUMN_USERNAME_SIZE + 1]; // decays to pointers when passed to sscanf
    char email[COLUMN_EMAIL_SIZE + 1];
} Row;

typedef struct
{
    StatementType type;
    Row row_to_insert;
} Statement;

const __uint32_t PAGE_SIZE = 4096;
#define MAX_TABLE_PAGES 100

typedef struct
{
    int file_descriptor;
    __uint32_t file_length;
    void *pages[MAX_TABLE_PAGES];
} Pager;

typedef struct
{
    __uint32_t num_rows;
    Pager *pager;
} Table;

const __uint32_t ID_SIZE = sizeof(((Row *)0)->id);
const __uint32_t USERNAME_SIZE = sizeof(((Row *)0)->username);
const __uint32_t EMAIL_SIZE = sizeof(((Row *)0)->email);
const __uint32_t ID_OFFSET = 0;
const __uint32_t USERNAME_OFFSET = ID_OFFSET + ID_SIZE;
const __uint32_t EMAIL_OFFSET = USERNAME_OFFSET + USERNAME_SIZE;
const __uint32_t ROW_SIZE = ID_SIZE + USERNAME_SIZE + EMAIL_SIZE;

const __uint32_t TABLE_MAX_ROWS = (PAGE_SIZE / ROW_SIZE) * MAX_TABLE_PAGES;

char *getInput(InputBuffer *input_buffer);
InputBuffer *createInputBuffer();
PrepareResult prepareCommand(InputBuffer *input_buffer, Statement *statement);
ExecuteResult execute_statement(Statement *statement, Table *table);
ExecuteResult execute_insert(Statement *statement, Table *table);
ExecuteResult execute_select(Statement *statement, Table *table);

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
    Pager *pager = malloc(sizeof(pager));
    pager->file_descriptor = fd;
    pager->file_length = filesize; // byte offset
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

void *rowSlot(Pager *pager, int rownum)
{
    __uint32_t pagenum = rownum / (PAGE_SIZE / ROW_SIZE);

    if (pagenum > MAX_TABLE_PAGES)
        exit(EXIT_FAILURE);

    if (pager->pages[pagenum] == NULL)
    {
        // cache miss. our informatoin is not in memory. go and load from files
        void *page = malloc(PAGE_SIZE);
        __uint32_t numpagesavaliable = pager->file_length / PAGE_SIZE;
        if (pager->file_length % PAGE_SIZE)
            numpagesavaliable += 1;

        if (pagenum <= numpagesavaliable)
        {
            lseek(pager->file_descriptor, pagenum * PAGE_SIZE, SEEK_SET);
            ssize_t bytes = read(pager->file_descriptor, page, PAGE_SIZE);
            if (bytes == -1)
                exit(EXIT_FAILURE);
        }
        pager->pages[pagenum] = page;
    }

    __uint32_t rowoffset = rownum % (PAGE_SIZE / ROW_SIZE);
    __uint32_t byteoffset = rowoffset * ROW_SIZE;
    return pager->pages[pagenum] + byteoffset; // LOCATION OF NEW ROW
}

ExecuteResult execute_insert(Statement *statement, Table *table)
{
    if (table->num_rows >= TABLE_MAX_ROWS)
        return EXECUTE_TABLE_FULL;
    Row *row_to_insert = &(statement->row_to_insert);
    writeRow(row_to_insert, rowSlot(table, table->num_rows));
    table->num_rows += 1;

    return EXECUTE_SUCCESS;
}

ExecuteResult execute_select(Statement *statement, Table *table)
{
    Row temp;

    for (int i = 0; i < table->num_rows; i++)
    {
        int pagenum = i / (PAGE_SIZE / ROW_SIZE);
        void *page = table->pager->pages[pagenum];
        readRow(rowSlot(table, i), &temp);
        printf("(%d, %s, %s )\n", temp.id, temp.username, temp.email);
    }
    return EXECUTE_SUCCESS;
}

void db_close(Table *table)
{
    Pager *pager = table->pager;
    __uint32_t number_full_pages = table->num_rows / (PAGE_SIZE / ROW_SIZE);
    for (__uint32_t i = 0; i < number_full_pages; i++)
    {
        if (pager->pages[i] != NULL)
        {
            pager_flush(pager, i, PAGE_SIZE);
            free(pager->pages[i]);
            pager->pages[i] = NULL;
        }
    }

    __uint32_t remainder_rows = table->num_rows % (PAGE_SIZE / ROW_SIZE);
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