#include <stdio.h>
#include <signal.h>
#include <setjmp.h>
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <linux/limits.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <dirent.h>
#include <errno.h>
#include <pwd.h>
#include <readline/readline.h>
#include <readline/history.h>

#define SHELL_NAME "Microshell"
#define SHELL_VERSION "0.0.1"
#define LINE_PARTS_BUFF_SIZE 16
#define LINE_PARTS_DELIMETER " "
#define CP_BUFFER_SIZE 1024
#define PS_READ_BUFFER_SIZE 80
#define HISTORY_READ_BUFFER_SIZE 80
#define MAX_USERNAME_LENGTH 32

#define ANSI_COLOR_GREEN   "\001\033[0;32m\002"
#define ANSI_COLOR_CYAN    "\001\033[0;36m\002"
#define ANSI_COLOR_RESET   "\001\033[0m\002"

/*
    
    Initial author: Patryk Malczewski
*/

void handle_sigint(int sig);
void shell_save_history();
void shell_loop();
void unexpected_error(char* message);
char* shell_read_line();
char* get_command_prompt();
char** shell_separate_line();
void shell_run(char** args);
int get_number_of_shell_builtins();
void shell_cd(char** args);
void shell_perror(char* command_name);
void shell_exit();
void shell_help(char** args);
void shell_cp(char** args);
void shell_cp_regular_file(char* source, char* destination);
void shell_cp_directory(char* source, char* destination);
void shell_cp_file(char* source, char* destination);
void shell_close_directory(DIR* directory_to_close, char* command_name);
void shell_close_file(FILE* file, char* command_name);
void shell_ps(char** args);
void shell_head(char** args);
void shell_history(char** args);
void shell_exec(char** args);
void shell_load_history_from_file();
char* concat(char* a, char* b, char* c);
int does_string_contains_only_numbers(char* text);
char* get_file_content(char* file_path, char* command_name);
char* get_home_directory();

char* shell_builtins[] = {
    "cd",
    "exit",
    "help",
    "cp",
    "ps",
    "head",
    "history"
};

void (*shell_builtins_functions[]) (char** args) = {
    &shell_cd,
    &shell_exit,
    &shell_help,
    &shell_cp,
    &shell_ps,
    &shell_head,
    &shell_history
};

int main() {
    umask(0022);
    shell_load_history_from_file();
    shell_loop();
    return 0;
}

void shell_save_history() {
    char* history_file_path = concat(get_home_directory(), "/", ".microshell_history");
    if(write_history(history_file_path) != 0) {
        shell_perror("save history to file");
    }
    free(history_file_path);
}

void shell_load_history_from_file() {
    char* history_file_path = concat(get_home_directory(), "/", ".microshell_history");
    read_history(history_file_path);
    free(history_file_path);
}

void shell_loop() {
    char* line;
    char** line_parts;

    while(1) {
        line = shell_read_line();
        line_parts = shell_separate_line(line);
        add_history(line);
        shell_run(line_parts);
        free(line);
        free(line_parts);
    }
}

char* shell_read_line() {
    char* line;
    char* command_prompt = get_command_prompt();
    line = readline(command_prompt);
    if(line == NULL) {
        shell_exit();
    }
    free(command_prompt);
    return line;
} 

char* get_command_prompt() {
    char cwd[PATH_MAX + 1];
    getcwd(cwd, sizeof(cwd));
    char* username = getlogin();
    char* characters_to_add = "[:" ANSI_COLOR_CYAN ANSI_COLOR_RESET ANSI_COLOR_GREEN ANSI_COLOR_RESET "] $ ";
    int command_prompt_length = strlen(cwd)+strlen(characters_to_add)+strlen(username)+1;
    char* command_prompt = (char*) malloc(sizeof(char)*(command_prompt_length));
    strcpy(command_prompt, "[");
    strcat(command_prompt, ANSI_COLOR_CYAN);
    strcat(command_prompt, username);
    strcat(command_prompt, ANSI_COLOR_RESET);
    strcat(command_prompt, ":");
    strcat(command_prompt, ANSI_COLOR_GREEN);
    strcat(command_prompt, cwd);
    strcat(command_prompt, ANSI_COLOR_RESET);
    strcat(command_prompt, "] $ ");
    command_prompt[command_prompt_length-1] = '\0';
    return command_prompt;
}

void unexpected_error(char* error_message) {
    fprintf(stderr, "[%s] %s \n", SHELL_NAME, error_message);
    exit(EXIT_FAILURE);
}

char** shell_separate_line(char* line) {
    int parts_array_position = 0;
    int parts_array_buffer_size = LINE_PARTS_BUFF_SIZE;
    char** parts = (char**)malloc(LINE_PARTS_BUFF_SIZE * sizeof(char*));
    if(parts == NULL) {
        unexpected_error("Memory allocation error");
    }
    int position = 0;
    int trim_start_position;
    int trim_end_position;
    while(position < strlen(line)) {
        if(line[position] == ' ') {
            position++;
            continue;
        } else if(line[position] == '"') {
            position++;
            trim_start_position = position;
            while(line[position] != '"' && line[position] != '\0') {
                position++;
            }
            if(line[position] == '\0') {
                parts[0] = NULL;
                break;
            } else {
                trim_end_position = position-1;
                position++;
            }
        } else if(line[position] == '\\') {
            position++;
            trim_start_position = position;
            while(line[position] != ' ' && line[position] != '\0') {
                position++;
            }
            trim_end_position = position-1;
        } else {
            trim_start_position = position;
            while(line[position] != ' ' && line[position] != '\0') {
                position++;
            }
            trim_end_position = position-1;
        }
        int part_length = trim_end_position - trim_start_position + 1;
        char* part = (char*) malloc((part_length + 1) * sizeof(char));
        if(part == NULL) {
            unexpected_error("Memory allocation error");
            break;
        }
        strncpy(part, line+trim_start_position, part_length);
        part[part_length] = '\0';
        parts[parts_array_position] = part;
        parts_array_position++;
        if(parts_array_position >= parts_array_buffer_size) {
            parts_array_buffer_size += LINE_PARTS_BUFF_SIZE;
            parts = realloc(parts, sizeof(char*) * parts_array_buffer_size);
            if(parts == NULL) {
                unexpected_error("Memory allocation error");
                break;
            }
        }
    }
    parts[parts_array_position] = NULL;
    return parts;
}

void shell_run(char** args) {
    if(args[0] == NULL) {
        fprintf(stderr, "[%s] Cannot parse command properly\n", SHELL_NAME);
        return;
    }

    char* command_name = args[0];
    int i;
    for(i=0; i<get_number_of_shell_builtins(); i++) {
        if(strcmp(command_name, shell_builtins[i]) == 0) {
            (*shell_builtins_functions[i])(args);
            return;
        }
    }
    shell_exec(args);
}

int get_number_of_shell_builtins() {
    return sizeof(shell_builtins) / sizeof(char*);
}

void shell_cd(char** args) {
    char* directory;
    if(args[1] == NULL || strcmp(args[1], "~") == 0) {
        char* homedir = getenv("HOME");
        if(homedir == NULL) {
            fprintf(stderr, "[%s] Home directory not specified\n", SHELL_NAME);
        }
        directory = homedir;
    } else {
        directory = args[1];
    }
    if(chdir(directory) == -1) {
        shell_perror(args[0]);
    }
}

void shell_perror(char* command_name) {
    /*EXAMPLE*/
    /*MICROSHELL: ls*/
    int message_size = strlen(SHELL_NAME) + 2 + strlen(command_name) + 1;
    char* perror_message = (char*) malloc(message_size * sizeof(char));
    strcpy(perror_message, SHELL_NAME);
    strcat(perror_message, ": ");
    strcat(perror_message, command_name);
    perror(perror_message);
    free(perror_message);
}

void shell_exit() {
    shell_save_history();
    printf("bye\n");
    exit(EXIT_SUCCESS);
}

void shell_help(char** args) {
    printf("---===   %s   ===---\n\n", SHELL_NAME);
    printf("Version %s\n", SHELL_VERSION);
    printf("Shell builtins: \n");
    printf("%-20s change current working directory\n", "cd [dir]");
    printf("%-20s close shell\n", "exit");
    printf("%-20s details about shell\n", "help");
    printf("%-20s copy files and directories\n", "cp [from] [to]");
    printf("%-20s list currently running processes\n", "ps");
    printf("%-20s output the first lines of file\n", "head [-n num] [file]");
    printf("%-20s show history\n", "history");

    printf("\nAdditional features:\n");
    printf("\t> Username in command propmpt\n");
    printf("\t> Colored command prompt\n");
    printf("\t> Move through commands history by pressing arrows\n");
    printf("\t> Filename completion on tab\n");
    printf("\t> Save history to file on exit (by EOF or exit command)\n");
    printf("\t> Parse arguments placed between quotes\n");

    printf("\nAUTHOR\n");
    printf("Created by Patryk Malczewski \n");
    printf("as operating systems final project \n");
    printf("at Adam Mickiewicz University AD 2023/2024\n");
}

void shell_cp(char** args) {
    if(args[1] == NULL) {
        fprintf(stderr, "%s: %s: Missing file operand \n", SHELL_NAME, args[0]);
        return;
    }
    if(args[2] == NULL) {
        fprintf(stderr, "%s: %s: Missing destination file operand \n", SHELL_NAME, args[0]);
        return;
    }
    char* source = args[1];
    char* destination = args[2];
    shell_cp_file(source, destination);
}

/* CP FUNCTIONS BLOCK START*/

void shell_cp_file(char* source, char* destination) {
    if(access(source, R_OK) == -1) {
        shell_perror("cp");
        return;
    }
    struct stat source_status;
    stat(source, &source_status);
    if(S_ISREG(source_status.st_mode)) {
        shell_cp_regular_file(source, destination);
    } else if(S_ISDIR(source_status.st_mode)) {
        shell_cp_directory(source, destination);
    } else {
        fprintf(stderr, "%s: %s: Unsupported file type\n", SHELL_NAME, "cp");
    }
}

void shell_cp_regular_file(char* source, char* destination) {
    int source_file = open(source, O_RDONLY);
    if(source_file == -1) {
        shell_perror("cp");
        return;
    }
    int destination_file = creat(destination, 0666);
    if(destination_file == -1) {
        shell_perror("cp");
        return;
    }
    char buffer[CP_BUFFER_SIZE];
    int number_of_readed_bytes, number_of_writed_bytes;
    while(1) {
        number_of_readed_bytes = read(source_file, buffer, CP_BUFFER_SIZE);
        if(number_of_readed_bytes == -1) {
            shell_perror("cp");
            break;
        }
        if(number_of_readed_bytes == 0) {
            break;
        }
        number_of_writed_bytes = write(destination_file, buffer, number_of_readed_bytes);
        if(number_of_writed_bytes == -1) {
            shell_perror("cp");
            break;
        }
    }
    if(close(source_file) == -1) {
        shell_perror("cp");
    }
    if(close(destination_file) == -1) {
        shell_perror("cp");
    }
}

void shell_cp_directory(char* source, char* destination) {
    DIR *source_directory = opendir(source);
    if(source_directory == NULL) {
        shell_perror("cp");
        shell_close_directory(source_directory, "cp");
        return;
    }
    if(mkdir(destination, 0777) == -1) {
        shell_perror("cp");
        shell_close_directory(source_directory, "cp");
        return;
    }

    struct dirent *entry;
    while(1) {
        errno = 0;
        entry = readdir(source_directory);
        if(entry == NULL) {
            if(errno != 0) {
                shell_perror("cp");
            }
            break;
        }
        if(strcmp(".", entry->d_name) == 0 || strcmp("..", entry->d_name) == 0) {
            continue;
        }
        char* source_path = concat(source, "/", entry->d_name);
        char* destination_path = concat(destination, "/", entry->d_name);
        shell_cp_file(source_path, destination_path);
        free(source_path);
        free(destination_path);
    }

    shell_close_directory(source_directory, "cp");
}

void shell_close_directory(DIR* directory_to_close, char* command_name) {
    if(closedir(directory_to_close) == -1) {
        shell_perror(command_name);
    }
}

void shell_close_file(FILE* file, char* command_name) {
    if(fclose(file) == EOF) {
        shell_perror(command_name);
    }
}

/* CP FUNCTIONS BLOCK END*/

void shell_ps(char** args) {
    DIR *proc_directory = opendir("/proc");
    if(proc_directory == NULL) {
        shell_perror("ps");
        shell_close_directory(proc_directory, "ps");
        return;
    }
    struct dirent *entry;
    printf("%-6s CMD\n", "PID");
    while(1) {
        errno = 0;
        entry = readdir(proc_directory);
        if(entry == NULL) {
            if(errno != 0) {
                shell_perror("ps");
            }
            break;
        }
        if(!does_string_contains_only_numbers(entry->d_name)) {
            continue;
        }
        char* stat_file_path = concat("/proc/", entry->d_name, "/stat");
        FILE* stat_file;
        stat_file = fopen(stat_file_path, "r");
        int pid;
        char* cmd = (char*)malloc(PS_READ_BUFFER_SIZE * sizeof(char));
        int cmd_buffor_size = PS_READ_BUFFER_SIZE;
        int position = 0;
        if(stat_file != NULL) {
            fscanf(stat_file, "%d ", &pid);
            char c;
            do {
                c = fgetc(stat_file);
                if(c == '(' || c == ')') {
                    continue;
                }
                cmd[position] = c;
                position++;
                if(position >= cmd_buffor_size) {
                    cmd_buffor_size += PS_READ_BUFFER_SIZE;
                    cmd = realloc(cmd, cmd_buffor_size);
                }
            } while(c != ' ');
            cmd[position] = '\0';
            printf("%-6d %s\n", pid, cmd);
            free(cmd);
        } else {
            shell_perror("ps");
        }
        shell_close_file(stat_file, "ps");
        free(stat_file_path);
    }
    shell_close_directory(proc_directory, "ps");
}

void shell_head(char **args) {
    int number_of_lines_to_print = 10;
    int input = STDIN_FILENO;
    if(args[1] != NULL) {
        if(strcmp(args[1], "-n") == 0) {
            char* number_of_lines = args[2];
            if(number_of_lines != NULL) {
                number_of_lines_to_print = atoi(number_of_lines);
            }
            if(args[3] != NULL) {
                input = open(args[3], O_RDONLY);
            }
        } else {
            input = open(args[1], O_RDONLY);
        }
    }

    if(input == -1) {
        shell_perror("head");
        return;
    }

    int c;
    int number_of_readed_bytes;
    int number_of_printed_lines = 0;
    while(1) {
        number_of_readed_bytes = read(input, &c, 1);
        if(number_of_readed_bytes == -1) {
            shell_perror("head");
            break;
        }
        if(number_of_readed_bytes == 0) {
            break;
        }
        printf("%c", c);
        if(((char)c) == '\n') {
            number_of_printed_lines++;
        }
        if(number_of_printed_lines == number_of_lines_to_print || c == EOF) {
            break;
        }
    }

    if(input != STDIN_FILENO) {
        if(close(input) == -1) {
            shell_perror("head");
        }
    }
}

void shell_history(char** args) {
    if(history_set_pos(0) == 0) {
        return;
    }
    HIST_ENTRY* hist_entry = current_history();
    int counter = 1;
    while(hist_entry != NULL) {
        printf("%-5d %s\n", counter, hist_entry->line);
        counter++;
        hist_entry = next_history();
    }
}

void shell_exec(char **args) {
    pid_t pid = fork();
    if(pid<0) {
        fprintf(stderr, "[%s] Cannot create child process\n", SHELL_NAME);
        return;
    } else if(pid == 0) {
        if(execvp(args[0], args) == -1) {
            fprintf(stderr, "%s: %s: command not found\n", SHELL_NAME, args[0]);
            exit(EXIT_FAILURE);
        }
    } else {
        waitpid(pid, NULL, 0);
    }
}

/* SHELL UTILS */

char* concat(char* a, char* b, char* c) {
    char* result = malloc((strlen(a)+strlen(b)+strlen(c)+1)*sizeof(char));
    result[strlen(a)+strlen(b)+strlen(c)] = '\0';
    strcpy(result, a);
    strcat(result, b);
    strcat(result, c);
    return result;
}

int does_string_contains_only_numbers(char* text) {
    int i, character;
    for(i=0; i<strlen(text); i++) {
        character = (int) text[i];
        if(character < 48 || character > 57) {
            return 0;
        }
    }
    return 1;
}

char* get_home_directory() {
    char* home_directory;
    if((home_directory = getenv("HOME")) == NULL) {
        home_directory = getpwuid(getuid())->pw_dir;
    }
    return home_directory;
}
