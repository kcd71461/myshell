#include <stdio.h>
#include <string.h>
#include <malloc.h>
#include <stdbool.h>
#include <unistd.h>
#include <stdlib.h>
#include <wait.h>
#include <fcntl.h>

#define BUFFER_SIZE 1000
#define MAX_TOKENS 20

int execute(char *command, int readPipe, int writePipe, bool defaultIsBackground);

int processCommand(char *command);

void trimNewLine(char *line);

int splitString(char *line, char delimiter, char ***pResult);

void freeSplitedCommands(char **pString, int count);

int main(int argc, char **argv) {
    if (argc > 1) {
        char command[BUFFER_SIZE];
        strcpy(command, argv[1]);

        int i;
        for (i = 2; i < argc; i++) {
            if (argv[i] != NULL) {
                strcat(command, " ");
                strcat(command, argv[i]);
            }
        }
        processCommand(command);
    } else {
        while (true) {
            putchar('$');
            fflush(stdout);
            char line[BUFFER_SIZE];
            if (!fgets(line, sizeof(line) - 1, stdin))
                break;

            if (line[0] == '\n') {
                continue;
            }
            processCommand(line);
        }
        printf("\n");
    }
}

/**
 * splited commands(char**)을 메모리 해제
 * @param pString
 * @param count
 */
void freeSplitedCommands(char **pString, int count) {
    int i;
    for (i = 0; i < count; i++) {
        free(pString[i]);
    }
    free(pString);
}

/**
 * 커맨드 라인 실행
 * @param command
 * @return
 */
int processCommand(char *command) {
    char **splitedCommands;
    int splitedCommandCounts = splitString(command, '|', &splitedCommands);
    if (splitedCommandCounts == 1) {
        execute(splitedCommands[0], -1, -1, false);
    } else {
        int pipeCount = splitedCommandCounts - 1;
        int pipes[pipeCount][2];
        int i;
        for (i = 0; i < splitedCommandCounts; i++) {
            if (i < pipeCount) {
                //<editor-fold desc="pipe 생성">
                if (pipe(pipes[i]) < 0) {
                    printf("pipe error!");
                    break;
                }
                //</editor-fold>
            }

            int readFd = i > 0 ? pipes[i - 1][0] : -1;
            int writeFd = i < pipeCount ? pipes[i][1] : -1;
            execute(splitedCommands[i], readFd, writeFd, false);
        }
    }
    freeSplitedCommands(splitedCommands, splitedCommandCounts);
}

/**
 * 파이프는 분리된 하나의 라인을 실행
 * @param command
 * @param readPipe
 * @param writePipe
 * @param defaultIsBackground
 * @return
 */
int execute(char *command, int readPipe, int writePipe, bool defaultIsBackground) {
    trimNewLine(command);
    int in, out, savedIn, savedOut; // fds
    char *inFileName, *outFileName;
    bool needToSetInput = false;
    bool needToSetOutput = false;
    bool needToSetError = false;
    bool isBackground = defaultIsBackground;

    char *save_ptr;
    char *next_ptr = strtok_r(command, " ", &save_ptr);
    char *args[MAX_TOKENS + 1];

    int i;
    for (i = 0; i <= MAX_TOKENS; i++) {
        if (next_ptr != NULL) {
            if (strcmp(next_ptr, "<") == 0) {
                //<editor-fold desc="<">
                needToSetInput = true;

                int j;
                for (j = i; j <= MAX_TOKENS; j++) {
                    args[j] = NULL;
                }

                next_ptr = strtok_r(NULL, " ", &save_ptr);
                inFileName = next_ptr;

                next_ptr = strtok_r(NULL, " ", &save_ptr);

                if (next_ptr == NULL) {
                    break;
                }
                //</editor-fold>
            }
            if (strcmp(next_ptr, ">") == 0 || strcmp(next_ptr, "2>") == 0) {
                //<editor-fold desc="> or 2>">
                if (strcmp(next_ptr, ">") == 0) {
                    needToSetOutput = true;
                } else if (strcmp(next_ptr, "2>") == 0) {
                    needToSetError = true;
                }

                int j;
                for (j = i; j <= MAX_TOKENS; j++) {
                    args[j] = NULL;
                }
                next_ptr = strtok_r(NULL, " ", &save_ptr);
                outFileName = next_ptr;
                next_ptr = strtok_r(NULL, " ", &save_ptr);
                if (next_ptr == NULL) {
                    break;
                }
                //</editor-fold>
            }
            if (strcmp(next_ptr, "&") == 0) {
                //<editor-fold desc="&">
                isBackground = true;
                int j;
                for (j = i; j <= MAX_TOKENS; j++) {
                    args[j] = NULL;
                }
                next_ptr = strtok_r(NULL, " ", &save_ptr);
                break;
                //</editor-fold>
            }
        }

        args[i] = next_ptr;

        if (next_ptr == NULL || needToSetOutput || needToSetError || isBackground) {
            break;
        }
        next_ptr = strtok_r(NULL, " ", &save_ptr);
    }
    args[MAX_TOKENS] = NULL;

    if (needToSetInput) {
        //<editor-fold desc="in file descriptor">
        if (in < 0) {
            in = open(inFileName, O_RDONLY);
        }
        savedIn = dup(STDIN_FILENO);
        dup2(in, STDIN_FILENO);
        close(in);
        //</editor-fold>
    }
    if (needToSetOutput) {
        //<editor-fold desc="out file descriptor">
        if (out < 0) {
            out = open(outFileName, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);
        }

        savedOut = dup(STDOUT_FILENO);
        dup2(out, STDOUT_FILENO);
        close(out);
        //</editor-fold>
    } else if (needToSetError) {
        //<editor-fold desc="err file descriptor">
        out = open(outFileName, O_WRONLY | O_TRUNC | O_CREAT, S_IRUSR | S_IRGRP | S_IWGRP | S_IWUSR);

        savedOut = dup(STDERR_FILENO);
        dup2(out, STDERR_FILENO);
        close(out);
        //</editor-fold>
    }
    if (readPipe >= 0) { // read pipe 설정
        savedIn = dup(STDIN_FILENO);
        dup2(readPipe, STDIN_FILENO);
        close(readPipe);
    }
    if (writePipe >= 0) { // write pipe 설정
        savedOut = dup(STDOUT_FILENO);
        dup2(writePipe, STDOUT_FILENO);
        close(writePipe);
    }

    pid_t pid;
    pid = fork();

    if (pid == 0) {
        //<editor-fold desc="child proc">
        int execResult = execvp(args[0], &args[0]);
        if (execResult < 0) {
            printf("Error %s\n", args[0]);
            exit(1);
        }
        exit(0);
        //</editor-fold>
    } else {
        //<editor-fold desc="parent proc">
        if (needToSetInput || readPipe >= 0) {
            dup2(savedIn, STDIN_FILENO);
            close(savedIn);
        }
        if (needToSetOutput || writePipe >= 0) {
            dup2(savedOut, STDOUT_FILENO);
            close(savedOut);
        } else if (needToSetError) {
            dup2(savedOut, STDERR_FILENO);
            close(savedOut);
        }

        int exit_status;
        if (!isBackground) {
            pid_t wait_result = waitpid(pid, &exit_status, 0);
            if (wait_result < 0) {
                printf("Error, proc id: %d\n", pid);
                exit(1);
            }
        }
        //</editor-fold>
    }
}

/**
 * 개행 trim
 * @param line
 */
void trimNewLine(char *line) {
    char *pos;
    if ((pos = strchr(line, '\n')) != NULL) {
        *pos = '\0';
    }
}

/**
 * delimiter로 string을 split
 * @param line
 * @param delimiter
 * @param pResult
 * @return split length
 */
int splitString(char *line, char delimiter, char ***pResult) {
    int count = 1;
    size_t loopCount = strlen(line);
    if (line[loopCount - 1] == '\n') {
        loopCount--;
    }

    if (loopCount == 0) {
        return 0;
    }

    int i;
    for (i = 0; i < loopCount; i++) {
        if (line[i] == delimiter) {
            count++;
        }
    }
    *pResult = malloc(sizeof(char *) * count);
    char **result = *pResult;
    int assignIndex = 0;
    int startIndex = 0;
    for (i = 0; i < loopCount; i++) {
        if (line[i] == delimiter || i == (loopCount - 1)) { // 구분자를 만나거나 마지막까지 index 이동시
            int charArrayLength = (i - startIndex + 2);
            if (line[i] == delimiter) {
                charArrayLength -= 1;
            }
            result[assignIndex] = (char *) malloc(sizeof(char) * charArrayLength); // \0도 포함
            memcpy(result[assignIndex], &line[startIndex], (size_t) (charArrayLength - 1));
            result[assignIndex][charArrayLength - 1] = 0;
            startIndex = i + 1;
            assignIndex++;
        }
    }
    return count;
}