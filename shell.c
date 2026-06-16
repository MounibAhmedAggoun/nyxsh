#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <signal.h>
#include <fcntl.h>
#include <ctype.h>
#include <termios.h>
#include <sys/ioctl.h>
#include <glob.h>
#include <dirent.h>

#define MAX_INPUT 4096
#define MAX_ARGS 256
#define MAX_JOBS 32
#define MAX_HISTORY 500
#define MAX_ALIASES 50
#define MAX_GLOB_MATCHES 1024
#define HISTORY_FILE ".shell_history"

// Alias structure
typedef struct {
    char name[128];
    char value[512];
} Alias;

// Job structure for background process tracking
typedef struct {
    pid_t pid;
    pid_t pgid;
    int job_id;
    char command[512];
    int status; // 0: running, 1: stopped, 2: done
    int notified;
} Job;

// Global state
Job jobs[MAX_JOBS];
int job_count = 0;
int last_exit_status = 0;
char *history[MAX_HISTORY];
int history_count = 0;
int history_index = 0;  // For history navigation
Alias aliases[MAX_ALIASES];
int alias_count = 0;
pid_t shell_pid;
pid_t shell_pgid;
pid_t current_fg_pid = 0;
int shell_terminal;
struct termios saved_termios;
int interactive = 1;

// Signal handlers
void handle_sigchld(int sig) {
    int status;
    pid_t pid;
    while ((pid = waitpid(-1, &status, WNOHANG)) > 0) {
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].pid == pid) {
                if (WIFEXITED(status)) {
                    jobs[i].status = 2;
                } else if (WIFSTOPPED(status)) {
                    jobs[i].status = 1;
                }
                jobs[i].notified = 0;
                break;
            }
        }
    }
}

void handle_sigint(int sig) {
    if (current_fg_pid > 0) {
        kill(-current_fg_pid, SIGINT);
    } else {
        printf("\n");
        fflush(stdout);
    }
}

void handle_sigtstp(int sig) {
    if (current_fg_pid > 0) {
        kill(-current_fg_pid, SIGTSTP);
    }
}

void handle_sigcont(int sig) {
    // Resume handling
}

// Terminal control - enter raw mode
void enter_raw_mode(void) {
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &raw) == -1) return;
    saved_termios = raw;
    
    raw.c_lflag &= ~(ECHO | ICANON);
    raw.c_cc[VMIN] = 1;
    raw.c_cc[VTIME] = 0;
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &raw);
}

// Terminal control - exit raw mode
void exit_raw_mode(void) {
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &saved_termios);
}

// Memory management
void free_args(char **args) {
    if (args) {
        for (int i = 0; args[i] != NULL; i++) {
            free(args[i]);
        }
    }
}

// Glob expansion (*, ?, [abc])
int expand_glob(char *pattern, char **results, int max_results) {
    glob_t glob_result;
    int return_value = glob(pattern, GLOB_NOCHECK | GLOB_TILDE, NULL, &glob_result);
    
    if (return_value != 0) {
        results[0] = strdup(pattern);
        return 1;
    }
    
    int count = (glob_result.gl_pathc > max_results) ? max_results : glob_result.gl_pathc;
    for (int i = 0; i < count; i++) {
        results[i] = strdup(glob_result.gl_pathv[i]);
    }
    
    globfree(&glob_result);
    return count;
}

// Add command to history
void add_to_history(char *command) {
    if (!command || strlen(command) == 0) return;
    
    // Avoid duplicates
    if (history_count > 0 && strcmp(history[history_count - 1], command) == 0) {
        return;
    }
    
    if (history_count < MAX_HISTORY) {
        history[history_count] = malloc(strlen(command) + 1);
        if (history[history_count]) {
            strcpy(history[history_count], command);
            history_count++;
        }
    } else {
        free(history[0]);
        for (int i = 0; i < MAX_HISTORY - 1; i++) {
            history[i] = history[i + 1];
        }
        history[MAX_HISTORY - 1] = malloc(strlen(command) + 1);
        if (history[MAX_HISTORY - 1]) {
            strcpy(history[MAX_HISTORY - 1], command);
        }
    }
    history_index = history_count;
}

// Load history from file
void load_history(void) {
    char home[512];
    char *h = getenv("HOME");
    if (!h) h = "/root";
    snprintf(home, sizeof(home), "%s/%s", h, HISTORY_FILE);
    
    FILE *f = fopen(home, "r");
    if (!f) return;
    
    char line[MAX_INPUT];
    while (fgets(line, sizeof(line), f) && history_count < MAX_HISTORY) {
        line[strcspn(line, "\n")] = '\0';
        if (strlen(line) > 0) {
            history[history_count] = malloc(strlen(line) + 1);
            if (history[history_count]) {
                strcpy(history[history_count], line);
                history_count++;
            }
        }
    }
    fclose(f);
    history_index = history_count;
}

// Save history to file
void save_history(void) {
    char home[512];
    char *h = getenv("HOME");
    if (!h) h = "/root";
    snprintf(home, sizeof(home), "%s/%s", h, HISTORY_FILE);
    
    FILE *f = fopen(home, "w");
    if (!f) return;
    
    for (int i = 0; i < history_count; i++) {
        fprintf(f, "%s\n", history[i]);
    }
    fclose(f);
}

// Expand history (!!, !$, !n)
char* expand_history(const char *input) {
    static char expanded[MAX_INPUT];
    strcpy(expanded, input);
    
    // !! = last command
    if (strstr(input, "!!")) {
        if (history_count > 0) {
            char *last = history[history_count - 1];
            char *ptr = strstr(expanded, "!!");
            if (ptr) {
                *ptr = '\0';
                strcat(expanded, last);
                strcat(expanded, ptr + 2);
                printf("%s\n", expanded);
            }
        }
        return expanded;
    }
    
    // !$ = last argument of last command
    if (strstr(input, "!$")) {
        if (history_count > 0) {
            char *last = history[history_count - 1];
            char *last_arg = strrchr(last, ' ');
            if (last_arg) last_arg++; else last_arg = last;
            
            char *ptr = strstr(expanded, "!$");
            if (ptr) {
                *ptr = '\0';
                strcat(expanded, last_arg);
                strcat(expanded, ptr + 2);
                printf("%s\n", expanded);
            }
        }
        return expanded;
    }
    
    // !n = nth command
    if (input[0] == '!' && isdigit(input[1])) {
        int n = atoi(input + 1);
        if (n > 0 && n <= history_count) {
            strcpy(expanded, history[n - 1]);
            printf("%s\n", expanded);
        }
        return expanded;
    }
    
    return (char *)input;
}

// Enhanced input function with line editing
int read_input_enhanced(char *input, size_t max_len) {
    int pos = 0;
    input[0] = '\0';
    
    while (pos < max_len - 1) {
        int ch = getchar();
        
        // Enter - complete input
        if (ch == '\n' || ch == '\r') {
            input[pos] = '\0';
            printf("\n");
            return pos;
        }
        
        // Backspace
        if (ch == 127 || ch == '\b') {
            if (pos > 0) {
                pos--;
                printf("\b \b");
                fflush(stdout);
            }
            continue;
        }
        
        // Tab - basic completion
        if (ch == '\t') {
            // Simple path completion
            if (pos > 0) {
                // Try to find matching files
                printf("\n");
                return pos;  // For now, just accept
            }
            continue;
        }
        
        // Printable character
        if (ch >= 32 && ch <= 126) {
            input[pos++] = ch;
            printf("%c", ch);
            fflush(stdout);
            continue;
        }
        
        // Ctrl+A - beginning of line
        if (ch == 1) {
            printf("\r");
            printf("shell> ");
            pos = 0;
            continue;
        }
        
        // Ctrl+E - end of line
        if (ch == 5) {
            printf("\r");
            printf("shell> %s", input);
            continue;
        }
        
        // Ctrl+K - kill to end of line
        if (ch == 11) {
            input[pos] = '\0';
            printf("\033[K");
            fflush(stdout);
            continue;
        }
        
        // Ctrl+C
        if (ch == 3) {
            printf("\n");
            return -1;
        }
        
        // Escape sequence (arrow keys)
        if (ch == 27) {
            ch = getchar();
            if (ch == '[') {
                ch = getchar();
                // Up arrow - previous command
                if (ch == 'A' && history_index > 0) {
                    history_index--;
                    strcpy(input, history[history_index]);
                    pos = strlen(input);
                    printf("\r");
                    printf("shell> %s", input);
                    printf("\033[K");
                    fflush(stdout);
                }
                // Down arrow - next command
                else if (ch == 'B' && history_index < history_count - 1) {
                    history_index++;
                    strcpy(input, history[history_index]);
                    pos = strlen(input);
                    printf("\r");
                    printf("shell> %s", input);
                    printf("\033[K");
                    fflush(stdout);
                }
                // Down arrow at end of history - show blank line
                else if (ch == 'B' && history_index == history_count - 1) {
                    history_index = history_count;
                    input[0] = '\0';
                    pos = 0;
                    printf("\r");
                    printf("shell> ");
                    printf("\033[K");
                    fflush(stdout);
                }
                // Right arrow
                else if (ch == 'C' && pos < strlen(input)) {
                    pos++;
                    printf("\033[C");
                }
                // Left arrow
                else if (ch == 'D' && pos > 0) {
                    pos--;
                    printf("\033[D");
                }
            }
            continue;
        }
    }
    
    input[pos] = '\0';
    return pos;
}

// Find alias
char* find_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            return aliases[i].value;
        }
    }
    return NULL;
}

// Add or update alias
void add_alias(const char *name, const char *value) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            strncpy(aliases[i].value, value, 511);
            return;
        }
    }
    if (alias_count < MAX_ALIASES) {
        strncpy(aliases[alias_count].name, name, 127);
        strncpy(aliases[alias_count].value, value, 511);
        alias_count++;
    } else {
        fprintf(stderr, "Error: alias table full\n");
    }
}

// Remove alias
void remove_alias(const char *name) {
    for (int i = 0; i < alias_count; i++) {
        if (strcmp(aliases[i].name, name) == 0) {
            for (int j = i; j < alias_count - 1; j++) {
                aliases[j] = aliases[j + 1];
            }
            alias_count--;
            return;
        }
    }
}

// Parse input
int parse_input_advanced(char *input, char **args) {
    int arg_count = 0;
    int i = 0, j = 0;
    char current_arg[1024];
    int in_quotes = 0;
    char quote_char = 0;
    
    while (i < strlen(input) && arg_count < MAX_ARGS - 1) {
        while (i < strlen(input) && isspace(input[i])) i++;
        if (i >= strlen(input)) break;
        
        // Check for here-doc
        if (input[i] == '<' && i + 1 < strlen(input) && input[i + 1] == '<') {
            if (j > 0) {
                current_arg[j] = '\0';
                args[arg_count++] = strdup(current_arg);
                j = 0;
            }
            args[arg_count++] = strdup("<<");
            i += 2;
            continue;
        }
        
        // Check for stderr redirection
        if (input[i] == '2' && i + 1 < strlen(input) && input[i + 1] == '>' && !in_quotes) {
            if (j > 0) {
                current_arg[j] = '\0';
                args[arg_count++] = strdup(current_arg);
                j = 0;
            }
            if (i + 3 < strlen(input) && input[i + 2] == '&' && input[i + 3] == '1') {
                args[arg_count++] = strdup("2>&1");
                i += 4;
            } else {
                args[arg_count++] = strdup("2>");
                i += 2;
            }
            continue;
        }
        
        // Check for operators
        if ((input[i] == '|' || input[i] == '>' || input[i] == '<' || input[i] == '&' || input[i] == ';') && !in_quotes) {
            if (j > 0) {
                current_arg[j] = '\0';
                args[arg_count++] = strdup(current_arg);
                j = 0;
            }
            
            if (input[i] == '>' && i + 1 < strlen(input) && input[i + 1] == '>') {
                args[arg_count++] = strdup(">>");
                i += 2;
            } else {
                char op[3] = {input[i], '\0', '\0'};
                args[arg_count++] = strdup(op);
                i++;
            }
            continue;
        }
        
        // Handle quotes
        if ((input[i] == '"' || input[i] == '\'') && (i == 0 || input[i-1] != '\\')) {
            if (!in_quotes) {
                in_quotes = 1;
                quote_char = input[i];
            } else if (input[i] == quote_char) {
                in_quotes = 0;
            }
            i++;
            continue;
        }
        
        // Handle escape sequences
        if (input[i] == '\\' && i + 1 < strlen(input)) {
            i++;
            current_arg[j++] = input[i++];
            continue;
        }
        
        // Handle variable expansion
        if (input[i] == '$' && !in_quotes) {
            int var_start = ++i;
            while (i < strlen(input) && (isalnum(input[i]) || input[i] == '_')) {
                i++;
            }
            char var_name[256];
            strncpy(var_name, input + var_start, i - var_start);
            var_name[i - var_start] = '\0';
            
            char *var_value = getenv(var_name);
            if (var_value) {
                int vlen = strlen(var_value);
                if (j + vlen < 1024) {
                    strcpy(current_arg + j, var_value);
                    j += vlen;
                }
            }
            continue;
        }
        
        if (j < 1023) current_arg[j++] = input[i++];
    }
    
    if (j > 0) {
        current_arg[j] = '\0';
        args[arg_count++] = strdup(current_arg);
    }
    
    args[arg_count] = NULL;
    return arg_count;
}

// Add job
int add_job(pid_t pid, pid_t pgid, char *command) {
    if (job_count >= MAX_JOBS) {
        fprintf(stderr, "Error: job table full\n");
        return -1;
    }
    int job_id = 1;
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id >= job_id) job_id = jobs[i].job_id + 1;
    }
    jobs[job_count].pid = pid;
    jobs[job_count].pgid = pgid;
    jobs[job_count].job_id = job_id;
    strncpy(jobs[job_count].command, command, 511);
    jobs[job_count].status = 0;
    jobs[job_count].notified = 0;
    job_count++;
    return job_id;
}

// Remove job
void remove_job(int job_id) {
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == job_id) {
            for (int j = i; j < job_count - 1; j++) {
                jobs[j] = jobs[j + 1];
            }
            job_count--;
            break;
        }
    }
}

// Built-in: echo
void builtin_echo(char **args) {
    int newline = 1;
    int interpret_escapes = 0;
    int arg_start = 1;
    
    while (arg_start < MAX_ARGS && args[arg_start] && args[arg_start][0] == '-') {
        for (int i = 1; args[arg_start][i]; i++) {
            if (args[arg_start][i] == 'n') newline = 0;
            else if (args[arg_start][i] == 'e') interpret_escapes = 1;
        }
        arg_start++;
    }
    
    for (int i = arg_start; args[i] != NULL; i++) {
        if (interpret_escapes) {
            for (int j = 0; args[i][j]; j++) {
                if (args[i][j] == '\\' && args[i][j + 1]) {
                    j++;
                    switch (args[i][j]) {
                        case 'n': printf("\n"); break;
                        case 't': printf("\t"); break;
                        case '\\': printf("\\"); break;
                        default: printf("%c", args[i][j]);
                    }
                } else {
                    printf("%c", args[i][j]);
                }
            }
        } else {
            printf("%s", args[i]);
        }
        if (args[i + 1] != NULL) printf(" ");
    }
    if (newline) printf("\n");
}

// Built-in: cd
void builtin_cd(char **args) {
    if (args[1] == NULL) {
        char *home = getenv("HOME");
        if (chdir(home ? home : "/") != 0) {
            perror("Error: cd failed");
        }
    } else if (strcmp(args[1], "-") == 0) {
        char *oldpwd = getenv("OLDPWD");
        if (oldpwd) {
            if (chdir(oldpwd) != 0) {
                perror("Error: cd failed");
            }
        } else {
            fprintf(stderr, "Error: OLDPWD not set\n");
        }
    } else if (args[1][0] == '~') {
        char path[1024];
        char *home = getenv("HOME");
        snprintf(path, sizeof(path), "%s%s", home ? home : "", args[1] + 1);
        if (chdir(path) != 0) {
            perror("Error: cd failed");
        }
    } else {
        if (chdir(args[1]) != 0) {
            perror("Error: cd failed");
        }
    }
}

// Built-in: pwd
void builtin_pwd(char **args) {
    char cwd[1024];
    if (getcwd(cwd, sizeof(cwd)) != NULL) {
        printf("%s\n", cwd);
    } else {
        perror("Error: pwd failed");
    }
}

// Built-in: history
void builtin_history(char **args) {
    for (int i = 0; i < history_count; i++) {
        printf("%d  %s\n", i + 1, history[i]);
    }
}

// Built-in: jobs with detailed status
void builtin_jobs(char **args) {
    for (int i = 0; i < job_count; i++) {
        char status_str[16];
        if (jobs[i].status == 0) strcpy(status_str, "Running");
        else if (jobs[i].status == 1) strcpy(status_str, "Stopped");
        else strcpy(status_str, "Done");
        
        printf("[%d]  %s   %s\n", jobs[i].job_id, status_str, jobs[i].command);
    }
}

// Built-in: fg with process group control
void builtin_fg(char **args) {
    int job_id = 1;
    if (args[1] != NULL) {
        job_id = atoi(args[1]);
    }
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == job_id) {
            int status;
            current_fg_pid = jobs[i].pid;
            
            // Set foreground process group
            tcsetpgrp(shell_terminal, jobs[i].pgid);
            
            // Send continue if stopped
            if (jobs[i].status == 1) {
                kill(-jobs[i].pgid, SIGCONT);
            }
            
            // Wait for process
            waitpid(jobs[i].pid, &status, WUNTRACED);
            
            // Restore shell as foreground
            tcsetpgrp(shell_terminal, shell_pgid);
            
            current_fg_pid = 0;
            
            if (WIFSTOPPED(status)) {
                jobs[i].status = 1;
            } else {
                remove_job(job_id);
            }
            return;
        }
    }
    fprintf(stderr, "Error: job %d not found\n", job_id);
}

// Built-in: bg
void builtin_bg(char **args) {
    int job_id = 1;
    if (args[1] != NULL) {
        job_id = atoi(args[1]);
    }
    for (int i = 0; i < job_count; i++) {
        if (jobs[i].job_id == job_id) {
            jobs[i].status = 0;
            kill(-jobs[i].pgid, SIGCONT);
            printf("[%d]+ %s &\n", job_id, jobs[i].command);
            return;
        }
    }
    fprintf(stderr, "Error: job %d not found\n", job_id);
}

// Built-in: help
void builtin_help(char **args) {
    printf("Built-in commands:\n");
    printf("  cd [dir]       - Change directory (~ and - supported)\n");
    printf("  pwd            - Print working directory\n");
    printf("  echo [-n] [-e] - Print text\n");
    printf("  history        - Show command history\n");
    printf("  jobs           - List background jobs\n");
    printf("  fg [job_id]    - Bring job to foreground\n");
    printf("  bg [job_id]    - Resume job in background\n");
    printf("  exit [code]    - Exit shell\n");
    printf("  help           - Show this help\n");
    printf("\nHistory expansion: !!, !$, !n\n");
    printf("Line editing: Up/Down arrows, Ctrl+A/E, Ctrl+K\n");
    printf("Redirection: >, >>, 2>, 2>&1, <\n");
    printf("Pipes: |\n");
}

// Built-in: export
void builtin_export(char **args) {
    if (args[1] == NULL) {
        extern char **environ;
        for (int i = 0; environ[i] != NULL; i++) {
            printf("export %s\n", environ[i]);
        }
        return;
    }
    char *eq = strchr(args[1], '=');
    if (eq) {
        int len = eq - args[1];
        char var_name[256];
        strncpy(var_name, args[1], len);
        var_name[len] = '\0';
        setenv(var_name, eq + 1, 1);
    } else {
        fprintf(stderr, "Error: export requires VAR=value format\n");
    }
}

// Built-in: exit
void builtin_exit(char **args) {
    save_history();
    int code = 0;
    if (args[1] != NULL) {
        code = atoi(args[1]);
    }
    exit(code);
}

// I/O redirection
int setup_redirection(char **args, int *stdin_fd, int *stdout_fd, int *stderr_fd) {
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "2>&1") == 0) {
            *stderr_fd = dup(STDOUT_FILENO);
            args[i] = NULL;
            return 1;
        }
        if (strcmp(args[i], "2>") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: 2> requires filename\n");
                return 0;
            }
            *stderr_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*stderr_fd < 0) {
                perror("Error: open");
                return 0;
            }
            args[i] = NULL;
            return 1;
        }
        if (strcmp(args[i], ">") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: > requires filename\n");
                return 0;
            }
            *stdout_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*stdout_fd < 0) {
                perror("Error: open");
                return 0;
            }
            args[i] = NULL;
            return 1;
        }
        if (strcmp(args[i], ">>") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: >> requires filename\n");
                return 0;
            }
            *stdout_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (*stdout_fd < 0) {
                perror("Error: open");
                return 0;
            }
            args[i] = NULL;
            return 1;
        }
        if (strcmp(args[i], "<") == 0) {
            if (args[i + 1] == NULL) {
                fprintf(stderr, "Error: < requires filename\n");
                return 0;
            }
            *stdin_fd = open(args[i + 1], O_RDONLY);
            if (*stdin_fd < 0) {
                perror("Error: open");
                return 0;
            }
            args[i] = NULL;
            return 1;
        }
    }
    return 1;
}

// Execute command with redirection
void execute_command_with_redirection(char **args, int background) {
    int stdin_fd = -1, stdout_fd = -1, stderr_fd = -1;
    
    // Check for built-ins
    if (strcmp(args[0], "cd") == 0) {
        builtin_cd(args);
        return;
    }
    if (strcmp(args[0], "pwd") == 0) {
        builtin_pwd(args);
        return;
    }
    if (strcmp(args[0], "echo") == 0) {
        builtin_echo(args);
        return;
    }
    if (strcmp(args[0], "history") == 0) {
        builtin_history(args);
        return;
    }
    if (strcmp(args[0], "jobs") == 0) {
        builtin_jobs(args);
        return;
    }
    if (strcmp(args[0], "fg") == 0) {
        builtin_fg(args);
        return;
    }
    if (strcmp(args[0], "bg") == 0) {
        builtin_bg(args);
        return;
    }
    if (strcmp(args[0], "export") == 0) {
        builtin_export(args);
        return;
    }
    if (strcmp(args[0], "help") == 0) {
        builtin_help(args);
        return;
    }
    if (strcmp(args[0], "exit") == 0) {
        builtin_exit(args);
        return;
    }
    
    // Setup redirection
    if (!setup_redirection(args, &stdin_fd, &stdout_fd, &stderr_fd)) {
        return;
    }
    
    // Fork and execute
    pid_t pid = fork();
    if (pid == -1) {
        perror("Error: fork failed");
        return;
    }
    
    if (pid == 0) {
        // Child process
        if (background) {
            setpgid(0, 0);
        } else {
            setpgid(0, shell_pgid);
            tcsetpgrp(shell_terminal, getpgid(0));
        }
        
        if (stdin_fd >= 0) {
            dup2(stdin_fd, STDIN_FILENO);
            close(stdin_fd);
        }
        if (stdout_fd >= 0) {
            dup2(stdout_fd, STDOUT_FILENO);
            close(stdout_fd);
        }
        if (stderr_fd >= 0) {
            dup2(stderr_fd, STDERR_FILENO);
            close(stderr_fd);
        }
        
        // Debug: print args
        // fprintf(stderr, "DEBUG: Executing args[0]='%s'\n", args[0]);
        
        if (execvp(args[0], args) == -1) {
            perror("Error: execvp failed");
            exit(EXIT_FAILURE);
        }
    } else {
        // Parent process
        if (stdin_fd >= 0) close(stdin_fd);
        if (stdout_fd >= 0) close(stdout_fd);
        if (stderr_fd >= 0) close(stderr_fd);
        
        pid_t pgid = background ? pid : shell_pgid;
        setpgid(pid, pgid);
        
        if (background) {
            char cmd_str[512];
            snprintf(cmd_str, sizeof(cmd_str), "%s", args[0]);
            for (int i = 1; args[i] != NULL && strlen(cmd_str) < 511; i++) {
                strcat(cmd_str, " ");
                strcat(cmd_str, args[i]);
            }
            int job_id = add_job(pid, pgid, cmd_str);
            if (job_id > 0) {
                printf("[%d] %d\n", job_id, pid);
            }
        } else {
            current_fg_pid = pid;
            int status;
            waitpid(pid, &status, WUNTRACED);
            current_fg_pid = 0;
            
            if (WIFSTOPPED(status)) {
                char cmd_str[512];
                snprintf(cmd_str, sizeof(cmd_str), "%s", args[0]);
                for (int i = 1; args[i] != NULL && strlen(cmd_str) < 511; i++) {
                    strcat(cmd_str, " ");
                    strcat(cmd_str, args[i]);
                }
                add_job(pid, pgid, cmd_str);
                printf("\n[+] Stopped: %s\n", cmd_str);
                tcsetpgrp(shell_terminal, shell_pgid);
            } else if (WIFEXITED(status)) {
                last_exit_status = WEXITSTATUS(status);
            }
        }
    }
}

// Execute command (handles pipes)
void execute_command(char **args, int background) {
    int pipe_index = -1;
    for (int i = 0; args[i] != NULL; i++) {
        if (strcmp(args[i], "|") == 0) {
            pipe_index = i;
            break;
        }
    }
    
    if (pipe_index == -1) {
        execute_command_with_redirection(args, background);
    } else {
        args[pipe_index] = NULL;
        char **cmd2 = args + pipe_index + 1;
        
        int pipefd[2];
        if (pipe(pipefd) == -1) {
            perror("Error: pipe failed");
            return;
        }
        
        pid_t pid1 = fork();
        if (pid1 == -1) {
            perror("Error: fork failed");
            return;
        }
        
        if (pid1 == 0) {
            setpgid(0, shell_pgid);
            close(pipefd[0]);
            dup2(pipefd[1], STDOUT_FILENO);
            close(pipefd[1]);
            if (execvp(args[0], args) == -1) {
                perror("Error: execvp failed");
                exit(EXIT_FAILURE);
            }
        }
        
        pid_t pid2 = fork();
        if (pid2 == -1) {
            perror("Error: fork failed");
            return;
        }
        
        if (pid2 == 0) {
            setpgid(0, shell_pgid);
            close(pipefd[1]);
            dup2(pipefd[0], STDIN_FILENO);
            close(pipefd[0]);
            if (execvp(cmd2[0], cmd2) == -1) {
                perror("Error: execvp failed");
                exit(EXIT_FAILURE);
            }
        }
        
        close(pipefd[0]);
        close(pipefd[1]);
        waitpid(pid1, NULL, WUNTRACED);
        waitpid(pid2, NULL, WUNTRACED);
    }
}

// Main shell loop
void shell_loop() {
    char input[MAX_INPUT];
    char *args[MAX_ARGS];
    
    while (1) {
        // Check for completed background jobs
        for (int i = 0; i < job_count; i++) {
            if (jobs[i].status == 0 && !jobs[i].notified) {
                int status;
                int ret = waitpid(jobs[i].pid, &status, WNOHANG);
                if (ret > 0) {
                    printf("\n[%d]+ Done   %s\n", jobs[i].job_id, jobs[i].command);
                    jobs[i].notified = 1;
                    remove_job(jobs[i].job_id);
                    i--;
                }
            }
        }
        
        // Display prompt
        printf("shell> ");
        fflush(stdout);
        
        // Read input with line editing
        int len = read_input_enhanced(input, MAX_INPUT);
        if (len < 0) continue;
        if (strlen(input) == 0) continue;
        
        // Expand history
        char *expanded = expand_history(input);
        strcpy(input, expanded);
        
        // Add to history
        add_to_history(input);
        
        // Check for background execution
        int background = 0;
        int input_len = strlen(input);
        if (input_len > 0 && input[input_len - 1] == '&') {
            background = 1;
            input[input_len - 1] = '\0';
        }
        
        // Parse input
        int arg_count = parse_input_advanced(input, args);
        
        if (arg_count > 0) {
            char *alias_val = find_alias(args[0]);
            if (alias_val) {
                free_args(args);
                parse_input_advanced(alias_val, args);
            }
            
            execute_command(args, background);
            free_args(args);
        }
    }
}

// Cleanup
void cleanup(void) {
    save_history();
    for (int i = 0; i < history_count; i++) {
        free(history[i]);
    }
    exit_raw_mode();
}

int main() {
    shell_pid = getpid();
    shell_pgid = getpgrp();
    shell_terminal = STDIN_FILENO;
    
    // Setup signal handlers
    signal(SIGCHLD, handle_sigchld);
    signal(SIGINT, handle_sigint);
    signal(SIGTSTP, handle_sigtstp);
    signal(SIGCONT, handle_sigcont);
    
    // Cleanup on exit
    atexit(cleanup);
    
    // Set default environment
    if (getenv("HOME") == NULL) {
        setenv("HOME", "/root", 1);
    }
    
    // Load history
    load_history();
    
    shell_loop();
    return 0;
}
