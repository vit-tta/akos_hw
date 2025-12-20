#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <errno.h>

#define MAX_DEPTH 100

int main() {
    // Создаем временную директорию
    if (mkdir("./test_dir", 0755) == -1 && errno != EEXIST) {
        perror("mkdir");
        exit(EXIT_FAILURE);
    }
    
    if (chdir("./test_dir") == -1) {
        perror("chdir");
        exit(EXIT_FAILURE);
    }
    
    // Создаем исходный файл
    int fd = open("a", O_CREAT | O_WRONLY, 0644);
    if (fd == -1) {
        perror("open a");
        exit(EXIT_FAILURE);
    }
    close(fd);
    
    printf("Создан исходный файл: a\n");
    
    char current_link[32] = "a";
    char new_link[32];
    int depth = 0;
    
    // Основной цикл согласно алгоритму задания
    while (depth < MAX_DEPTH) {
        // Создаем имя для новой символьной ссылки
        snprintf(new_link, sizeof(new_link), "link%d", depth);
        
        // Создаем символьную ссылку на текущий файл/ссылку
        if (symlink(current_link, new_link) == -1) {
            perror("symlink");
            break;
        }
        
        printf("Создана символьная ссылка: %s -> %s\n", new_link, current_link);
        
        // Пытаемся открыть новую ссылку
        fd = open(new_link, O_RDONLY);
        if (fd == -1) {
            if (errno == ELOOP) {
                printf("\nДостигнута максимальная глубина рекурсии!\n");
                printf("Глубина рекурсии: %d символьных ссылок\n", depth);
                break;
            } else {
                printf("Ошибка открытия %s: %s\n", new_link, strerror(errno));
                break;
            }
        }
        
        // Успешно открыли - закрываем файл
        close(fd);
        
        // Для следующей итерации новая ссылка становится текущей
        strcpy(current_link, new_link);
        depth++;
    }
    
    if (depth == MAX_DEPTH) {
        printf("Достигнут максимальный предел проверки (%d ссылок)\n", MAX_DEPTH);
    }
    
    // Очистка
    printf("\nОчистка временных файлов...\n");
    for (int i = 0; i < depth; i++) {
        char name[32];
        snprintf(name, sizeof(name), "link%d", i);
        unlink(name);
    }
    unlink("a");
    
    chdir("..");
    if (rmdir("test_dir") == -1) {
        perror("rmdir");
    }
    
    return 0;
}