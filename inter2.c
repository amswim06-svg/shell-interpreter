#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/types.h>
#include <fcntl.h>
#include <errno.h>

#define MAX_WORDS 100

typedef struct WordNode {
    char *word;
    struct WordNode *next;
} WordNode;

// Структура для отслеживания фоновых процессов
typedef struct BackgroundProcess {
    pid_t pid;
    char *command;
    struct BackgroundProcess *next;
} BackgroundProcess;

BackgroundProcess *bg_processes = NULL;

int is_special(int c) {
    return strchr("><|&!$^;:", c) != NULL;
}

void add_word(WordNode **head, WordNode **tail, char *word) {
    WordNode *node = malloc(sizeof(WordNode));
    node->word = strdup(word);
    node->next = NULL;
    
    if (!*head) *head = node;
    else (*tail)->next = node;
    *tail = node;
}

void print_and_free(WordNode *head) {
    WordNode *current = head;
    
    while (current != NULL) {
        WordNode *next = current->next;
        free(current->word);
        free(current);
        current = next;
    }
}

WordNode* read_line() {
    WordNode *head = NULL, *tail = NULL;
    int c, prev = -1;
    
    int size = 64;
    int len = 0;
    char *buf = malloc(size);
    int quotes = 0;
    
    printf("shell> ");
    
    while ((c = getchar()) != '\n' && c != EOF) {
        if (c == '"') {
            if (quotes) {
                if (len > 0) {
                    buf[len] = '\0';
                    add_word(&head, &tail, buf);
                    len = 0;
                }
                quotes = 0;
            } else {
                if (len > 0) {
                    buf[len] = '\0';
                    add_word(&head, &tail, buf);
                    len = 0;
                }
                quotes = 1;
            }
            continue;
        }
        
        if (quotes) {
            if (len + 1 >= size) {
                size *= 2;
                buf = realloc(buf, size);
            }
            buf[len++] = c;
            continue;
        }
        
        if (is_special(c)) {
            if (len > 0) {
                buf[len] = '\0';
                add_word(&head, &tail, buf);
                len = 0;
            }
            
            if (c == '>' && prev == '>') {
                if (tail && strcmp(tail->word, ">") == 0) {
                    free(tail->word);
                    tail->word = strdup(">>");
                }
            } else {
                char sym[2] = {c, '\0'};
                add_word(&head, &tail, sym);
            }
            prev = c;
            continue;
        }
        
        if (isspace(c)) {
            if (len > 0) {
                buf[len] = '\0';
                add_word(&head, &tail, buf);
                len = 0;
            }
        } else {
            if (len + 1 >= size) {
                size *= 2;
                buf = realloc(buf, size);
            }
            buf[len++] = c;
        }
        
        prev = c;
    }
    
    if (len > 0) {
        buf[len] = '\0';
        add_word(&head, &tail, buf);
    }
    
    free(buf);
    return head;
}

char** list_to_array(WordNode *head, int *count) {
    *count = 0;
    WordNode *current = head;
    
    while (current != NULL) {
        (*count)++;
        current = current->next;
    }
    
    if (*count == 0) return NULL;
    
    char **array = malloc((*count + 1) * sizeof(char*));
    current = head;
    
    for (int i = 0; i < *count; i++) {
        array[i] = strdup(current->word);
        current = current->next;
    }
    array[*count] = NULL;
    
    return array;
}

void free_array(char **array, int count) {
    if (array == NULL) return;
    
    for (int i = 0; i < count; i++) {
        free(array[i]);
    }
    free(array);
}

// Функция для добавления фонового процесса в список
void add_background_process(pid_t pid, char **args, int arg_count) {
    BackgroundProcess *new_process = malloc(sizeof(BackgroundProcess));
    new_process->pid = pid;
    
    // Создаем строку с командой для отображения
    int total_len = 0;
    for (int i = 0; i < arg_count && args[i] != NULL; i++) { 
        total_len += strlen(args[i]) + 1;
    }
    
    char *command = malloc(total_len + 1); 
    command[0] = '\0';
    
    for (int i = 0; i < arg_count && args[i] != NULL; i++) {
        if (i > 0) strcat(command, " "); // соединяет в одну строку команды и разделяет через пробел 
        strcat(command, args[i]);
    }
    
    new_process->command = command;
    new_process->next = bg_processes;
    bg_processes = new_process;
    
    printf("[Запущен фоновый процесс: %d]\n", pid);
}

// Функция для проверки завершенных фоновых процессов
void check_background_processes() {
    BackgroundProcess **current = &bg_processes;
    
    while (*current != NULL) { 
        int status;
        pid_t result = waitpid((*current)->pid, &status, WNOHANG);
        
        if (result > 0) {
            // Процесс завершился
            BackgroundProcess *completed = *current;
            
            printf("[Фоновый процесс %d закончен: %s с кодом %d]\n", 
                   completed->pid, completed->command, WEXITSTATUS(status));
            
            // Удаляем из списка
            *current = completed->next;
            free(completed->command);
            free(completed);
        } else if (result == 0) {
            // Процесс еще выполняется
            current = &((*current)->next);  // переходим к следующему элементу
        } else {
            // Ошибка
            current = &((*current)->next);
        }
    }
}

int execute_cd(char **args, int arg_count) {
    if (arg_count < 2) {
        if (chdir("/home") != 0) {
            perror("cd");
            return 1;
        }
    } 
    else {
        if (chdir(args[1]) != 0) {
            perror("cd");
            return 1;
        }
    }
    return 0;
}

int is_builtin_command(char *command) {
    return strcmp(command, "cd") == 0 || strcmp(command, "exit") == 0;
}

int execute_builtin(char **args, int arg_count) {
    if (strcmp(args[0], "cd") == 0) {
        return execute_cd(args, arg_count);
    } else if (strcmp(args[0], "exit") == 0) {
        printf("Выход \n");
        exit(0);
    }
    return -1;
}

void handle_redirection(char **args, int arg_count, int *input_fd, int *output_fd) {
    for (int i = 0; i < arg_count - 1; i++) {
        if (strcmp(args[i], ">") == 0) {
            *output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_TRUNC, 0644);
            if (*output_fd == -1) {
                perror("Ошибка открытия файла для записи");
                exit(1);
            }
            args[i] = NULL;
        } else if (strcmp(args[i], ">>") == 0) {
            *output_fd = open(args[i + 1], O_WRONLY | O_CREAT | O_APPEND, 0644);
            if (*output_fd == -1) {
                perror("Ошибка открытия файла для добавления");
                exit(1);
            }
            args[i] = NULL;
        } else if (strcmp(args[i], "<") == 0) {
            *input_fd = open(args[i + 1], O_RDONLY);
            if (*input_fd == -1) {
                perror("Ошибка открытия файла для чтения");
                exit(1);
            }
            args[i] = NULL;
        }
    }
}

int execute_single_command(char **args, int arg_count, int input_fd, int output_fd, int background) {
    if (arg_count == 0) return 0;
    
    if (is_builtin_command(args[0])) {
        return execute_builtin(args, arg_count);
    }
    
    pid_t pid = fork();
    
    if (pid == 0) {
        // Дочерний процесс
        int final_input_fd = input_fd;
        int final_output_fd = output_fd;
        handle_redirection(args, arg_count, &final_input_fd, &final_output_fd);
        
        if (final_input_fd != STDIN_FILENO) {
            dup2(final_input_fd, STDIN_FILENO);
            close(final_input_fd);
        }
        
        if (final_output_fd != STDOUT_FILENO) {
            dup2(final_output_fd, STDOUT_FILENO);
            close(final_output_fd);
        }
        
        char *exec_args[MAX_WORDS];
        int exec_count = 0;
        
        for (int i = 0; i < arg_count && args[i] != NULL; i++) {
            exec_args[exec_count++] = args[i];
        }
        exec_args[exec_count] = NULL;
        
        execvp(exec_args[0], exec_args);
        
        perror("Ошибка выполнения команды");
        exit(1);
    } else if (pid > 0) {
        if (background) {
            // Фоновый процесс - добавляем в список и не ждем завершения
            add_background_process(pid, args, arg_count);
            return 0;
        } else {
            // Обычный процесс - ждем завершения
            int status;
            waitpid(pid, &status, 0);
            if (WIFEXITED(status)) {
                return WEXITSTATUS(status);
            }
        }
    } else {
        perror("Ошибка создания процесса");
    }
    
    return 1;
}

int execute_pipeline(char **args1, int count1, char **args2, int count2, int background) {
    int pipe_fd[2];
    
    if (pipe(pipe_fd) == -1) {
        perror("Ошибка создания конвейера");
        return 1;
    }
    
    pid_t pid1 = fork();
    if (pid1 == 0) {
        close(pipe_fd[0]);
        dup2(pipe_fd[1], STDOUT_FILENO);
        close(pipe_fd[1]);
        
        execute_single_command(args1, count1, STDIN_FILENO, STDOUT_FILENO, 0);
        exit(1);
    }
    
    pid_t pid2 = fork();
    if (pid2 == 0) {
        close(pipe_fd[1]);
        dup2(pipe_fd[0], STDIN_FILENO);
        close(pipe_fd[0]);
        
        execute_single_command(args2, count2, STDIN_FILENO, STDOUT_FILENO, 0);
        exit(1);
    }
    
    close(pipe_fd[0]);
    close(pipe_fd[1]);
    
    if (background) {
        // Для фонового конвейера добавляем оба процесса
        add_background_process(pid1, args1, count1);
        add_background_process(pid2, args2, count2);
        return 0;
    } else {
        // Обычный конвейер - ждем завершения
        int status1, status2;
        waitpid(pid1, &status1, 0);
        waitpid(pid2, &status2, 0);
        
        return WEXITSTATUS(status2);
    }
}

void execute_commands(WordNode *word_list) {
    if (word_list == NULL) return;
    
    int word_count;
    char **words = list_to_array(word_list, &word_count);
    
    // Проверяем, является ли команда фоновой
    int background = 0;
    if (word_count > 0 && strcmp(words[word_count - 1], "&") == 0) {
        background = 1;
        words[word_count - 1] = NULL; // Удаляем '&' из аргументов
        word_count--;
    } else {
        // Проверяем, есть ли '&' не в конце (ошибка)
        for (int i = 0; i < word_count; i++) {
            if (strcmp(words[i], "&") == 0) {
                printf("Ошибка: символ '&' может быть только в конце команды\n");
                free_array(words, word_count);
                return;
            }
        }
    }
    
    // Проверяем наличие конвейера
    int pipe_position = -1;
    for (int i = 0; i < word_count; i++) {
        if (strcmp(words[i], "|") == 0) {
            if (pipe_position != -1) {
                printf("Ошибка: поддерживается только один конвейер\n");
                free_array(words, word_count);
                return;
            }
            pipe_position = i;
        }
    }
    
    if (pipe_position != -1) {
        char **cmd1 = malloc((pipe_position + 1) * sizeof(char*));
        char **cmd2 = malloc((word_count - pipe_position) * sizeof(char*));
        
        for (int i = 0; i < pipe_position; i++) {
            cmd1[i] = words[i];
        }
        cmd1[pipe_position] = NULL;
        
        for (int i = pipe_position + 1; i < word_count; i++) {
            cmd2[i - pipe_position - 1] = words[i];
        }
        cmd2[word_count - pipe_position - 1] = NULL;
        
        execute_pipeline(cmd1, pipe_position, cmd2, word_count - pipe_position - 1, background);
        
        free(cmd1);
        free(cmd2);
    } else {
        execute_single_command(words, word_count, STDIN_FILENO, STDOUT_FILENO, background);
    }
    
    free_array(words, word_count);
}

int main() {
    while (!feof(stdin)) {
        // Проверяем завершенные фоновые процессы ПЕРЕД чтением новой команды
        check_background_processes();
        
        WordNode *words = read_line();
        if (words) {
            execute_commands(words);
            print_and_free(words);
        }
        if (feof(stdin)) {
            printf("\nОкончание ввода\n");
            break;
        }
    }
    
    // Ждем завершения всех фоновых процессов перед выходом
    while (bg_processes != NULL) {
        int status;
        pid_t pid = waitpid(-1, &status, 0); // ждем завершения любого доч процесса
        if (pid > 0) { // значит доч процесс завершился
            BackgroundProcess **current = &bg_processes;
            while (*current != NULL) {
                if ((*current)->pid == pid) { // нашли процесс в списке
                    BackgroundProcess *completed = *current;
                    printf("[Фоновый процесс %d закончен: %s с кодом %d]\n", 
                           completed->pid, completed->command, WEXITSTATUS(status));
                    *current = completed->next;
                    free(completed->command);
                    free(completed);
                    break;
                }
                current = &((*current)->next);
            }
        }
    }
    
    return 0;
}