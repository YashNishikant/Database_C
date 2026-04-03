#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    char * buffer;
    int buffer_size;
    int input_size;
} InputBuffer;


char* getInput (InputBuffer* input_buffer);
InputBuffer* createInputBuffer();

InputBuffer* createInputBuffer(){

    InputBuffer *user_input = (InputBuffer*)malloc(sizeof(InputBuffer));
    user_input->buffer = NULL;
    user_input->buffer_size = 0;
    user_input->input_size = 0;

    return user_input;

}

char* getInput (InputBuffer* input_buffer){
    getline(&(input_buffer->buffer), &(input_buffer->buffer_size), stdin);
    input_buffer->input_size = strlen(input_buffer->buffer) - 1;
    input_buffer->buffer[input_buffer->input_size] = 0;
}


int main (int argc, char *argv[]){
    InputBuffer *input_buffer = createInputBuffer();

    while(input_buffer->buffer == NULL || strcmp(input_buffer->buffer, ".exit")!=0){
        printf("db > ");

        getInput(input_buffer);

        printf("You entered: %s\n", input_buffer->buffer);
    }
    free(input_buffer->buffer);
    free(input_buffer);
}