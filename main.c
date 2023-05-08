#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <fcntl.h>

#define MAX_ARGS 64
#define MAX_BUFFER 1024

// Function to tokenize the command line input
void tokenizeInput(char* input, char** tokens, char* delimiter) {
    char* token;
    int index = 0;
    int in_quotes = 0;

    token = strtok(input, delimiter);
    while (token != NULL) {
        if (token[0] == '\"') {
            in_quotes = 1;
            memmove(token, token + 1, strlen(token)); // Remove the leading double quote
        }

        int len = strlen(token);
        if (token[len - 1] == '\"') {
            in_quotes = 0;
            token[len - 1] = '\0'; // Remove the trailing double quote
        }

        tokens[index++] = token;

        if (!in_quotes)
            token = strtok(NULL, delimiter);
        else
            token = strtok(NULL, ""); // Continue from the last position
    }
    tokens[index] = NULL;
}

// Function to execute a command
void executeCommand(char** tokens, int input_fd, int output_fd) {
    pid_t pid = fork();

    if (pid == 0) {
        // Child process

        // Input redirection
        if (input_fd != STDIN_FILENO) {
            dup2(input_fd, STDIN_FILENO);
            close(input_fd);
        }

        // Output redirection
        if (output_fd != STDOUT_FILENO) {
            dup2(output_fd, STDOUT_FILENO);
            close(output_fd);
        }

        // Execute the command
        execvp(tokens[0], tokens);
        perror("execvp");
        exit(1);
    } else if (pid > 0) {
        // Parent process
        wait(NULL);
    } else {
        // Error forking
        perror("fork");
    }
}

// Function to handle pipes
void handlePipes(char** tokens1, char** tokens2) {
    int pipefd[2];

    if (pipe(pipefd) == -1) {
        perror("pipe");
        return;
    }

    pid_t pid1 = fork();
    if (pid1 == 0) {
        // Child process 1

        // Close the write end of the pipe
        close(pipefd[0]);

        // Redirect stdout to the write end of the pipe
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[1]);

        // Execute the first command
        execvp(tokens1[0], tokens1);
        perror("execvp");
        exit(1);
    } else if (pid1 > 0) {
        // Parent process

        pid_t pid2 = fork();
        if (pid2 == 0) {
            // Child process 2

            // Close the read end of the pipe
            close(pipefd[1]);

            // Redirect stdin to the read end of the pipe
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);

            // Execute the second command
            execvp(tokens2[0], tokens2);
            perror("execvp");
            exit(1);
        } else if (pid2 > 0) {
            // Parent process

            // Close both ends of the pipe
            close(pipefd[0]);
            close(pipefd[1]);

            // Wait for both child processes to finish
            wait(NULL);
            wait(NULL);
        } else {
            // Error forking
            perror("fork");
        }
    } else {
        // Error forking
        perror("fork");
    }
}

int handleOutputRedirection(char* token, char** tokens) {
    int output_fd;

    if (strcmp(token, ">") == 0) {
        // Output truncation
        output_fd = open(tokens[0], O_WRONLY | O_CREAT | O_TRUNC, 0644);
    } else if (strcmp(token, ">>") == 0) {
        // Output append
        output_fd = open(tokens[0], O_WRONLY | O_CREAT | O_APPEND, 0644);
    } else {
        // Invalid output redirection
        fprintf(stderr, "Invalid output redirection.\n");
        return -1;
    }

    if (output_fd == -1) {
        perror("open");
        return -1;
    }

    return output_fd;
}

int main() {
    char input[MAX_BUFFER];
    char* tokens[MAX_ARGS];

    while (1) {
        printf("> ");
        fflush(stdout);

        if (fgets(input, sizeof(input), stdin) == NULL) {
            break;
        }

        // Trim trailing newline character
        input[strcspn(input, "\n")] = '\0';

        // Skip empty lines
        if (strlen(input) == 0) {
            continue;
        }

        // Trim comments
        char* comment = strchr(input, '#');
        if (comment != NULL) {
            *comment = '\0';
        }

        // Tokenize the input
        tokenizeInput(input, tokens, " \t");

        // Handle CD command separately
        if (strcmp(tokens[0], "cd") == 0) {
            if (tokens[1] == NULL) {
                fprintf(stderr, "cd: missing directory\n");
            } else {
                if (chdir(tokens[1]) != 0) {
                    perror("chdir");
                }
            }
            continue;
        }

        int input_fd = STDIN_FILENO;
        int output_fd = STDOUT_FILENO;

        // Check for output redirection
        int i = 0;
        while (tokens[i] != NULL) {
            if (strcmp(tokens[i], ">") == 0 || strcmp(tokens[i], ">>") == 0) {
                // Get the output file name and handle output redirection
                output_fd = handleOutputRedirection(tokens[i], &tokens[i + 1]);
                if (output_fd == -1) {
                    break;
                }

                // Remove the output file and the redirection symbol from tokens
                tokens[i] = NULL;
                break;
            }
            i++;
        }

        // Handle pipes
        int hasPipe = 0;
        i = 0;
        while (tokens[i] != NULL) {
            if (strcmp(tokens[i], "|") == 0) {
                hasPipe = 1;
                tokens[i] = NULL; // Terminate the first command
                break;
            }
            i++;
        }

        if (hasPipe) {
            // Tokenize the second command
            char** tokens2 = &tokens[i + 1];

            // Execute commands with pipe
            handlePipes(tokens, tokens2);
        } else {
            // Execute single command
            executeCommand(tokens, input_fd, output_fd);
        }
    }

    return 0;
}
