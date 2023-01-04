/**
 * Authors:   Dhruv Rawat, Anirudh Singh
 * Created:   30.10.2022
 * Course:    IS F462: Network Programming 
 * 
 **/

#define _GNU_SOURCE

#include <sys/wait.h>
#include <sys/types.h>
#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <signal.h>
#include <termios.h>
#include <dirent.h>
#include <sys/resource.h>
#include <ctype.h>
#include <stdbool.h>
#include <limits.h>
#include <sys/signal.h>
#include <sys/stat.h>

// ANSI Colors used in the shell
#define RED   		"\033[0;31m"
#define GREEN 		"\033[0;32m"
#define YELLOW 		"\033[0;33m"
#define BLUE 		"\033[0;34m"
#define PURPLE 		"\033[0;35m"
#define CYAN 		"\033[0;36m"
#define INVERT		"\033[0;7m"
#define RESET  		"\e[0m" 
#define BOLD		"\e[1m"
#define ITALICS		"\e[3m"
#define UNDERLINE	"\e[4m"

#define BUFFER_SIZE 1024
#define HISTORY_SIZE 100
#define MAX_CMD_SIZE 512
#define SPACE_DELIM " \t"

char *CMD_DELIM = ";";  //delimiter for multiple commands in same line

typedef struct {        // atomic command
    int argNum;
    char **args;
    char *commandName;
    char *buffer;
    bool input, output, append, builtin;
    int inputFD, outputFD;
    char *inputFile, *outputFile;
} COMMAND;

typedef struct {        // group of atomic commands
    int commandNum;
    COMMAND **commands;
} COMMAND_LIST;

typedef struct {        // entire command input by the user
    int pipeType;
    char *buffer;
    COMMAND_LIST **cmdLists;
} FULL_COMMAND;

typedef struct {        // a history item, can contain multiple FULL_COMMAND
    int index;
    FULL_COMMAND *fullcmd;
} HISTORY_ITEM;

typedef struct {        // table for storing history
    int size, maxSize;
    HISTORY_ITEM **historyList;
} HISTORY_TABLE;

HISTORY_TABLE *hist;
char *PATH, **SHELL_ENV;

// builtin commands
int changeDir(COMMAND *cmd);
int help(COMMAND *cmd);
int exitShell(COMMAND *cmd);
int cmdType(COMMAND *cmd);
int clearShell(COMMAND *cmd);
int execHistory(COMMAND *cmd);
bool executeBuiltins(COMMAND *cmd);
void addBuiltinToHistory(COMMAND *cmd);

// command execution
void execAtomicCommand(COMMAND *cmd);
void execSinglePipe(COMMAND *input, COMMAND *output, bool bg);
void execMultiPipe(COMMAND_LIST *cmdList, int count, bool bg);
void execDoublePipe(COMMAND *input, COMMAND *output1, COMMAND *output2, bool bg);
void execTriplePipe(COMMAND *input, COMMAND *output1, COMMAND *output2, COMMAND *output3, bool bg);

// managing history table
HISTORY_TABLE *initTable(HISTORY_TABLE *ht, int sz);
void printHistory(HISTORY_TABLE *ht);
void insertInHT(HISTORY_TABLE *ht, FULL_COMMAND *cmd);

// helpers for parsing
char *trimString(char *str);
int countSinglePipes(char *buf);
char *cleanString(char *str);

// parse commands
COMMAND *readAtomicCommand(char *buf);
COMMAND_LIST *readMultiplePipes(char *buf);
void parser(char *buf);

// BUILTIN COMMANDS ------------------------------------------------------------------------------------------------------------------------
char *builtins[] = {"cd", "help", "exit", "logout", "quit", "clear", "type", "history", "!"};

int (*builtinCMD[])(COMMAND *) = {&changeDir, &help, &exitShell, &exitShell, &exitShell, &clearShell, &cmdType, &execHistory, &execHistory};

// returns number of builtin commands
int builtinCount() {        
  return sizeof(builtins) / sizeof(char *);
}

// function to change current directory
int changeDir(COMMAND *cmd) {
    char *newDir;
    if (cmd->args[1] == NULL) {
        newDir = "/";
    } else {
        newDir = cmd->args[1];
    }
    if (chdir(newDir) != 0) {
        perror(RED BOLD "cd");
        printf(RESET " ");
    } else {
        fprintf(stderr, GREEN BOLD "DIRECTORY changed succesfully\n" RESET);
        char *pwd = malloc(512 * sizeof(char));
        memset(pwd, '\0', 512);
        getcwd(pwd, 512);
        printf("New directory: %s\n", pwd);
    }
    return 1;
}

// function to print help message, prints all available builtins
int help(COMMAND *cmd) {
    printf(BOLD "------------------------------------------------------------------------------------------------------\n" RESET);
    printf(YELLOW BOLD "Shell v1.0\n");
    printf("Type command names and then arguments, and hit simply hit\n" RESET);
    printf("The following are built in:\n");

    for (int i = 0; i < builtinCount(); i++) {
        printf("%d. %s\n", i+1, builtins[i]);
    }
    return 1;
}

// function to exit the shell
int exitShell(COMMAND *cmd) {
    printf(YELLOW BOLD "Exiting the shell...\n" RESET);
    exit(0);
    return EXIT_SUCCESS;
}

// function to execute builtins
bool executeBuiltins(COMMAND *cmd) {
    bool flag = false;
    if (cmd == NULL)
        return false;
    for (int i = 0; i < builtinCount(); i++) {
        char *cmdName = cmd->commandName;
        cmdName = cleanString(cmdName);
        if (strcmp(cmdName, builtins[i]) == 0) {
            flag = true;
            addBuiltinToHistory(cmd);
            return (*builtinCMD[i])(cmd);
        }
    }
    return flag;
}

// function to return command type
int cmdType(COMMAND *cmd) {
    if (cmd->args[1] == NULL) {
        fprintf(stderr, RED BOLD "Expected argument to \"type\"\n" RESET);
    } else {
        char *checkCMD = cmd->args[1];
        bool flag = false;
        for (int i = 0; i < builtinCount(); i++) {
            if (strcmp(checkCMD, builtins[i]) == 0) {
                flag = true;
            }
        }
        if (flag)
            printf(CYAN BOLD "%s is a shell builtin\n" RESET, checkCMD);
        else {
            char *cmdPath = malloc(1024 * sizeof(char));
            char *temp = strdup(PATH);
            char *token = strtok(temp, ":");
            while(token!=NULL) {
                strcpy(cmdPath, token);
                strcat(cmdPath, "/");
                strcat(cmdPath, checkCMD);
                if(access(cmdPath, F_OK)==0)
                    break;
                token = strtok(NULL, ":");
            }
            if(token!=NULL) {
                printf(CYAN BOLD "%s is %s\n" RESET, checkCMD, cmdPath);
            } else {
                printf(RED BOLD "%s: Command doesn't exist\n" RESET, checkCMD);
            }
        }
    }
    return 1;
}

// function to clear screen
int clearShell(COMMAND *cmd) {
    printf("\e[1;1H\e[2J");
    return 1;
}

// execute a command from history
int execHistory(COMMAND *cmd) {
    if(cmd->argNum==1) printHistory(hist);
    else {
        int idx = atoi(cmd->args[1]);
        if(idx>hist->size)
            printf(YELLOW BOLD "No command found at index %d.\n" RESET, idx);
        else {
            char *buf = hist->historyList[idx-1]->fullcmd->buffer;
            parser(buf);
        }
    }
    return 1;
}

// add a command to history table
void addBuiltinToHistory(COMMAND *cmd) {
    if(cmd->commandName == NULL || !strcmp(cmd->commandName, "history") || !strcmp(cmd->commandName, "!"))
        return;
    COMMAND_LIST *cl = malloc(sizeof(COMMAND_LIST));
    cl->commandNum = 1;
    cl->commands = malloc(sizeof(COMMAND));
    cl->commands[0] = cmd;
    FULL_COMMAND *fullcmd = malloc(sizeof(FULL_COMMAND *));
    fullcmd->pipeType = 0;
    fullcmd->buffer = cmd->buffer;
    fullcmd->cmdLists = malloc(sizeof(COMMAND_LIST *));
    fullcmd->cmdLists[0] = cl;
    insertInHT(hist, fullcmd);
}

// execute an atomic command
void execAtomicCommand(COMMAND *cmd) {
    int sz = strlen(cmd->commandName);
    if(cmd->commandName[sz-1]=='\n') cmd->commandName[sz - 1] = '\0';
    if (cmd->builtin)
        return;
    if(cmd->inputFD!=0) {
        printf("Reading input from fd: %d\n", cmd->inputFD);
        dup2(cmd->inputFD, 0);
    }
    if(cmd->outputFD!=1) {
        printf("Sending output to fd: %d\n", cmd->outputFD);
        dup2(cmd->outputFD, 1);
    }
    if(cmd->inputFile!=NULL) {
        int INPUTFD = open(cmd->inputFile, O_RDONLY, 0664);
        if(INPUTFD<0) {
            perror(RED BOLD "INPUT FILE");
            printf(RESET " ");
        }
        else {
            dup2(INPUTFD, 0);
        }
    } else if (cmd->outputFile!=NULL) {
        int OUTPUTFD;
        if (cmd->append) {
            OUTPUTFD = open(cmd->outputFile, O_CREAT | O_WRONLY | O_APPEND, 0664);
            if (OUTPUTFD < 0) {
                perror(RED BOLD "OUTPUT FILE");
                printf(RESET " ");
                return;
            } else {
                dup2(OUTPUTFD, 1);
            }
        } else {
            OUTPUTFD = open(cmd->outputFile, O_CREAT | O_WRONLY | O_TRUNC, 0664);
            if (OUTPUTFD < 0) {
                perror(RED BOLD "OUTPUT FILE");
                printf(RESET " ");
            } else {
                dup2(OUTPUTFD, 1);
            }
        }
    }
    char *cmdPath = malloc(1024 * sizeof(char));
    char *temp = strdup(PATH);
    char *token = strtok(temp, ":");
    while(token!=NULL) {
        strcpy(cmdPath, token);
        strcat(cmdPath, "/");
        strcat(cmdPath, cmd->commandName);
        if(access(cmdPath, F_OK)==0)
            break;
        token = strtok(NULL, ":");
    }
    if (token != NULL) {
        execve(cmdPath, cmd->args, SHELL_ENV);
    }
    else {
        perror(RED BOLD "Command Not Found");
        raise(SIGKILL);
        printf(RESET " ");
    }
}

void execMultiPipe(COMMAND_LIST *cmdList, int count, bool bg) {
    int cmds = count;
    if (cmds < 1)
        exit(0);
    if(cmds == 1) {
        pid_t child = fork();
        if(child<0)
            exit(0);
        if(child == 0) {
            execAtomicCommand(cmdList->commands[count-1]);
        }
        else {
            int status;
            if(cmdList->commands[count-1]->inputFD!=0)
                close(cmdList->commands[count - 1]->inputFD);
            if(cmdList->commands[count-1]->outputFD!=1)
                close(cmdList->commands[count - 1]->outputFD);
            if(!bg) {
                waitpid(child, &status, 0);
                printf(PURPLE BOLD "PID: %d is over. Status: %d\n" RESET, child, status);
            }
            else {
                waitpid(child, &status, WNOHANG);
                printf(PURPLE BOLD "Command %s sent to background, PID: %d, Status: %d\n" RESET, cmdList->commands[count-1]->commandName, child, status);
            }
        }
        return;
    }    
    int pfd[cmds - 1][2];
    for(int i=1; i<=cmds; i++) {
        int p = pipe(pfd[i - 1]);
        if (i < cmds && p == -1) {
            printf("Error in pipe\n");
            exit(0);
        }
        if(i<cmds) {
            cmdList->commands[i-1]->outputFD = pfd[i-1][1];
            cmdList->commands[i]->inputFD = pfd[i-1][0];
        }
        pid_t child = fork();
        if(child<0)
            exit(0);
        if(child == 0) {
            execAtomicCommand(cmdList->commands[i-1]);
            break;
        } else {
            int status;
            if(i==1)
                close(pfd[i - 1][1]);
            else if(i<cmds) {
                close(pfd[i - 2][0]);
                close(pfd[i - 1][1]);
            } else
                close(pfd[i - 2][0]);
            if(!bg) {
                waitpid(child, &status, 0);
                printf(PURPLE BOLD "PID: %d is over. Status: %d\n" RESET, child, status);
            }
            else {
                waitpid(child, &status, WNOHANG);
                printf(PURPLE BOLD "Command %s sent to background, PID: %d, Status: %d\n" RESET, cmdList->commands[count-1]->commandName, child, status);
            }
        }
    }
    
}

void execDoublePipe(COMMAND *input, COMMAND *output1, COMMAND *output2, bool bg) {
    int pipe1[2], pipe2[2];
    size_t PIPE_SIZE = INT_MAX;
    int p1 = pipe(pipe1);
    if (p1 == -1) printf(RED BOLD "Cannot create pipe for '%s'\n" RESET, output1->commandName);
    int p2 = pipe(pipe2);
    if (p2 == -1) printf(RED BOLD "Cannot create pipe for '%s'\n" RESET, output2->commandName);
    input->outputFD = pipe1[1];
    output1->inputFD = pipe1[0];
    output2->inputFD = pipe2[0];
    pid_t child = fork();
    if (child == 0) {
        execAtomicCommand(input);
    } else if(child < 0) {
        printf(RED BOLD "Unable to create child in double pipe\n" RESET);
    } else {
        int status;
        close(pipe1[1]);
        if (!bg) {
            waitpid(child, &status, 0);
            printf(PURPLE BOLD "PID: %d is over. Status: %d\n" RESET, child, status);
        }
        else {
            printf(PURPLE BOLD "Command %s sent to background, PID: %d\n" RESET, input->commandName, child);
            waitpid(child, &status, WNOHANG);
        }
    }
    tee(pipe1[0], pipe2[1], PIPE_SIZE, 0);
    close(pipe2[1]);
}

void execTriplePipe(COMMAND *input, COMMAND *output1, COMMAND *output2, COMMAND *output3, bool bg) {
    int pipe1[2], pipe2[2], pipe3[3];
    size_t PIPE_SIZE = INT_MAX;
    int p1 = pipe(pipe1);
    if (p1 == -1) printf(RED BOLD "Cannot create pipe for '%s'\n" RESET, output1->commandName);
    int p2 = pipe(pipe2);
    if (p2 == -1) printf(RED BOLD "Cannot create pipe for '%s'\n" RESET, output2->commandName);
    int p3 = pipe(pipe3);
    if (p3 == -1) printf(RED BOLD "Cannot create pipe for '%s'\n" RESET, output3->commandName);
    input->outputFD = pipe1[1];
    output1->inputFD = pipe1[0];
    output2->inputFD = pipe2[0];
    output3->inputFD = pipe3[0];
    pid_t child = fork();
    if (child == 0) {   
        execAtomicCommand(input);
    } else if(child < 0) printf(RED BOLD "Unable to create child in triple pipe\n" RESET);
    else {  
        close(pipe1[1]);
        int status;
        if (!bg) {
            waitpid(child, &status, 0);
            printf(PURPLE BOLD "PID: %d is over. Status: %d\n" RESET, child, status);
        }
        else {
            printf(PURPLE BOLD "Command %s sent to background, PID: %d\n" RESET, input->commandName, child);
            waitpid(child, &status, WNOHANG);
        }
    }
    tee(pipe1[0], pipe2[1], PIPE_SIZE, 0);
    close(pipe2[1]);
    tee(pipe1[0], pipe3[1], PIPE_SIZE, 0);
    close(pipe3[1]);
}

HISTORY_TABLE *initTable(HISTORY_TABLE *ht, int sz) {
    ht = malloc(sizeof(HISTORY_TABLE *));
    ht->size = 0;
    ht->maxSize = sz;
    ht->historyList = malloc(sz * (sizeof(HISTORY_ITEM *)));
    return ht;
}

void printHistory(HISTORY_TABLE *ht) {
    printf("Printing history\n");
    int n = ht->size;
    for(int i=0; i<n; i++) {
        printf("[%d] %s\n", i+1, ht->historyList[i]->fullcmd->buffer);
    }
    return;
}

void insertInHT(HISTORY_TABLE *ht, FULL_COMMAND *cmd) {
    HISTORY_ITEM *item = malloc(sizeof(HISTORY_ITEM *));
    item->index = ht->size;
    item->fullcmd = cmd;
    ht->size++;
    ht->historyList[item->index] = item;
}

void prompt() {
	char *usr = getenv("USER");
	char *pwd = malloc(512 * sizeof(char));
	getcwd(pwd, 512);
	printf(GREEN BOLD "%s" RESET ":" BLUE BOLD "%s" RESET "$ ",usr,pwd);
	free(pwd);
}

void shell() {
	size_t MAXSIZE = 1024;
	do {
		prompt();
		char *cmd = malloc(MAXSIZE * sizeof(char *));
		getline(&cmd, &MAXSIZE, stdin);
		if (cmd[0] == '\n' || cmd == NULL) {
			printf(RED BOLD "No character entered!\n" RESET);
			continue;
		}
        char *tokenizer;
        char *temp = strdup(cmd);
        char *token = strtok_r(temp, CMD_DELIM, &tokenizer);
        while(token!=NULL) {
            token = cleanString(token);
            printf("Parsing:" YELLOW BOLD "%s" RESET "\n", token);
            parser(token);
            token = strtok_r(NULL, CMD_DELIM, &tokenizer);
            printf(BOLD "------------------------------------------------------------------------------------------------------\n" RESET);
        }
	} while (1);
}

void childHandler() {
	int wstat;
	pid_t pid;
	while (1) {
		pid = wait3(&wstat, WNOHANG, NULL);
		if (pid == 0 || pid == -1) return;
		printf(PURPLE BOLD "[%d] Background process OVER. Status: %d\n" RESET, pid, wstat);
		prompt();
		fflush(stdout);
	}
}

int main(int argc, char **argv, char **envp) {
	signal(SIGCHLD, childHandler);
	SHELL_ENV = envp;
	PATH = getenv("PATH");
    hist = initTable(hist, HISTORY_SIZE);
	shell();
	return EXIT_SUCCESS;
}

void getSubstring(char* str, char* substring , int beg, int end){
    memcpy(substring, &str[beg], end);
    substring[end] = '\0';
}

char *trimString(char *str) {
    int i = 0, n = strlen(str);
    while(i<n && (str[i]=='|' || str[i]==' '))
        i++;
    char *sub = (char*)malloc(sizeof(char)*(n-i+1));
    getSubstring(str, sub, i, n);
    return sub;
}

char *cleanString(char *str) {
    if(str==NULL)
        return NULL;
    int sz = strlen(str);
    if(!isalnum(str[sz-1]) && str[sz-1]!='!' && str[sz-1]!='.')
        str[sz - 1] = '\0';
    return str;
}

int countSinglePipes(char *buf) {
    int sz = strlen(buf), count=0;
    for (int i = 0; i < sz; i++) {
        if(buf[i]=='|')
            count++;
    }
    return count;
}

COMMAND *readAtomicCommand(char *buf) {
    if(buf==NULL)
        return NULL;
    COMMAND *cmd = malloc(sizeof(COMMAND));
    char *tokenizer, *ipFile = NULL, *opFile = NULL, *iptokbuf, *optokbuf;
    char *temp = strdup(buf);
    char *temp2 = strdup(buf);
    char *token = strtok_r(temp2, " ><", &tokenizer);
    while(token != NULL) {
        int argID = 0;
        char *inputTok = strstr(temp, "<");
        if(inputTok!=NULL) {
            cmd->input = true;
            inputTok++;
            inputTok = trimString(inputTok);
            ipFile = strtok_r(inputTok, SPACE_DELIM, &iptokbuf);
        }
        temp = strdup(buf);
        char *outputTok = strstr(temp, ">");
        if(outputTok!=NULL) {
            cmd->output = true;
            outputTok++;
            if(*outputTok=='>') {
                cmd->append = true;
                outputTok++;
            }
            else
                cmd->append = false;
            outputTok = trimString(outputTok);
            opFile = strtok_r(outputTok, SPACE_DELIM, &optokbuf);
        }
        cmd->commandName = token;
        temp = strdup(buf);
        char *toks = strtok_r(temp, "><", &tokenizer);
        int argNum = 1, i = 0;
        while(toks[i]!='\0') {
            if(toks[i]==' ')
                argNum++;
            i++;
        }
        cmd->buffer = buf;
        cmd->argNum = argNum;
        cmd->inputFD = 0;
        cmd->outputFD = 1;
        cmd->inputFile = cleanString(ipFile);
        cmd->outputFile = cleanString(opFile);
        cmd->args = malloc((argNum + 1) * sizeof(char *));
        char *argTok = strtok_r(toks, SPACE_DELIM, &tokenizer);
        while(argTok!=NULL) {
            cmd->args[argID] = cleanString(argTok);
            argID++;
            argTok = strtok_r(NULL, SPACE_DELIM, &tokenizer);
        }        
        cmd->args[cmd->argNum] = '\0';
        cmd->builtin = false;
        return cmd;
    }
    return NULL;
}

COMMAND_LIST *readMultiplePipes(char *buf) {
    if(buf == NULL) return NULL;
    char *tokenizer;
    char *temp = strdup(buf);
    int pipecount = countSinglePipes(temp);
    COMMAND_LIST *complexCommand = malloc(sizeof(COMMAND_LIST));
    complexCommand->commandNum = pipecount+1;
    complexCommand->commands = malloc((pipecount+1)*sizeof(COMMAND));
    int commandID = 0, tksize = 0;
    char *token = strtok_r(temp, "|", &tokenizer);
    while(token!=NULL) {
        tksize = strlen(token);
        if(token[0]==' ')
            token++;
        if(token[tksize-1]==' ') token[tksize - 1] = '\0';
        complexCommand->commands[commandID++] = readAtomicCommand(token);
        token = strtok_r(NULL, "|", &tokenizer);
    }
    return complexCommand;
}

void parser(char *buf) {
    char *listbuf = strdup(buf);
    int sz = strlen(buf);
    COMMAND_LIST *inputDP, *outputDP0, *outputDP1, *outputDP2;
    char *firstToken = malloc((MAX_CMD_SIZE+1)*sizeof(char));
    memset(firstToken, '\0', sizeof(firstToken));
    if (buf == NULL) return;
    bool isbg = false;
    if (buf[sz - 1] == '&') {
        isbg = true;
        buf[sz - 1] = '\0';
    }
    char *tokenizer;
    char *temp = strdup(buf);
    char *dpTOKEN = strstr(temp, "||");
    if(dpTOKEN == NULL) firstToken = temp;
    else {
        // strncpy(firstToken, temp, dpTOKEN - temp);
        snprintf(firstToken, dpTOKEN - temp, "%s", temp);
    }
    inputDP = readMultiplePipes(firstToken);
    if(executeBuiltins(inputDP->commands[0])) return;
    if (dpTOKEN != NULL) {
        int pipeType = 2;
        char *separator; 
        if(dpTOKEN[0]=='|' && dpTOKEN[1]=='|' && dpTOKEN[2]=='|')
            pipeType = 3;
        dpTOKEN = trimString(dpTOKEN);
        separator = strtok_r(dpTOKEN, ",", &tokenizer);
        if(separator!=NULL) {
            outputDP0 = readMultiplePipes(separator);
            dpTOKEN = trimString(dpTOKEN);
            separator = strtok_r(NULL, ",", &tokenizer);
            if(separator!=NULL) {
                outputDP1 = readMultiplePipes(separator);
                if(pipeType==3) {
                    dpTOKEN = trimString(dpTOKEN);
                    separator = strtok_r(NULL, ",", &tokenizer);
                    if(separator!=NULL) {
                        outputDP2 = readMultiplePipes(separator);
                        if(inputDP->commandNum>1) {
                            int pfd[2];
                            if(pipe(pfd)==-1)
                                exit(0);
                            inputDP->commands[inputDP->commandNum - 2]->outputFD = pfd[1];
                            inputDP->commands[inputDP->commandNum - 1]->inputFD = pfd[0];
                            execMultiPipe(inputDP, inputDP->commandNum-1, isbg);
                        }
                        execTriplePipe(inputDP->commands[inputDP->commandNum - 1], outputDP0->commands[0], outputDP1->commands[0], outputDP2->commands[0], isbg);
                        execMultiPipe(outputDP0, outputDP0->commandNum, isbg);
                        execMultiPipe(outputDP1, outputDP1->commandNum, isbg);
                        execMultiPipe(outputDP2, outputDP2->commandNum, isbg);
                        FULL_COMMAND *cmd = malloc(sizeof(FULL_COMMAND *));
                        cmd->pipeType = 3;
                        cmd->buffer = listbuf;
                        cmd->cmdLists = malloc(4 * sizeof(COMMAND_LIST *));
                        cmd->cmdLists[0] = inputDP;
                        cmd->cmdLists[1] = outputDP0;
                        cmd->cmdLists[2] = outputDP1;
                        cmd->cmdLists[3] = outputDP2;
                        insertInHT(hist, cmd);
                    }
                } else {
                    if(inputDP->commandNum>1) {
                        int pfd[2];
                        if(pipe(pfd)==-1)
                            exit(0);
                        inputDP->commands[inputDP->commandNum - 2]->outputFD = pfd[1];
                        inputDP->commands[inputDP->commandNum - 1]->inputFD = pfd[0];
                        execMultiPipe(inputDP, inputDP->commandNum-1, isbg);
                    }
                    execDoublePipe(inputDP->commands[inputDP->commandNum - 1], outputDP0->commands[0], outputDP1->commands[0], isbg);
                    execMultiPipe(outputDP0, outputDP0->commandNum, isbg);
                    execMultiPipe(outputDP1, outputDP1->commandNum, isbg);
                    FULL_COMMAND *cmd = malloc(sizeof(FULL_COMMAND *));
                    cmd->pipeType = 2;
                    cmd->buffer = listbuf;
                    cmd->cmdLists = malloc(3 * sizeof(COMMAND_LIST *));
                    cmd->cmdLists[0] = inputDP;
                    cmd->cmdLists[1] = outputDP0;
                    cmd->cmdLists[2] = outputDP1;
                    insertInHT(hist, cmd);
                }
            }
        }
    }
    else  {
        execMultiPipe(inputDP, inputDP->commandNum, isbg);
        FULL_COMMAND *cmd = malloc(sizeof(FULL_COMMAND *));
        cmd->pipeType = 1;
        cmd->buffer = listbuf;
        cmd->cmdLists = malloc(sizeof(COMMAND_LIST *));
        cmd->cmdLists[0] = inputDP;
        insertInHT(hist, cmd);
    }
}

