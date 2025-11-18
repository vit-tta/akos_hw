#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <string.h>
#include <errno.h>

#define BUFFER_SIZE 32

// Функция для копирования файла с большим буфером
int copy_file_large_buffer(const char *src_path, const char *dst_path) {
    int src_fd, dst_fd;
    struct stat st;
    
    // Получаем информацию о исходном файле
    if (stat(src_path, &st) == -1) {
        perror("stat source file");
        return -1;
    }
    
    // Определяем размер буфера (больше размера файла)
    size_t buffer_size = st.st_size + 1024;
    char *buffer = malloc(buffer_size);
    if (!buffer) {
        perror("malloc");
        return -1;
    }
    
    // Открываем исходный файл для чтения
    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        perror("open source file");
        free(buffer);
        return -1;
    }
    
    // Открываем целевой файл для записи
    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        perror("open destination file");
        close(src_fd);
        free(buffer);
        return -1;
    }
    
    // Читаем весь файл за один раз
    ssize_t bytes_read = read(src_fd, buffer, buffer_size);
    if (bytes_read == -1) {
        perror("read");
        close(src_fd);
        close(dst_fd);
        free(buffer);
        return -1;
    }
    
    // Записываем прочитанные данные
    ssize_t bytes_written = write(dst_fd, buffer, bytes_read);
    if (bytes_written == -1) {
        perror("write");
        close(src_fd);
        close(dst_fd);
        free(buffer);
        return -1;
    }
    
    // Закрываем файлы и освобождаем память
    close(src_fd);
    close(dst_fd);
    free(buffer);
    
    return 0;
}

// Функция для копирования файла с маленьким буфером (32 байта)
int copy_file_small_buffer(const char *src_path, const char *dst_path) {
    int src_fd, dst_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    
    // Открываем исходный файл для чтения
    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        perror("open source file");
        return -1;
    }
    
    // Открываем целевой файл для записи
    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (dst_fd == -1) {
        perror("open destination file");
        close(src_fd);
        return -1;
    }
    
    // Читаем и записываем файл по частям
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            close(src_fd);
            close(dst_fd);
            return -1;
        }
    }
    
    if (bytes_read == -1) {
        perror("read");
        close(src_fd);
        close(dst_fd);
        return -1;
    }
    
    // Закрываем файлы
    close(src_fd);
    close(dst_fd);
    
    return 0;
}

// Функция для копирования файла с сохранением прав доступа
int copy_file_preserve_permissions(const char *src_path, const char *dst_path) {
    int src_fd, dst_fd;
    char buffer[BUFFER_SIZE];
    ssize_t bytes_read, bytes_written;
    struct stat st;
    
    // Получаем информацию о исходном файле
    if (stat(src_path, &st) == -1) {
        perror("stat source file");
        return -1;
    }
    
    // Открываем исходный файл для чтения
    src_fd = open(src_path, O_RDONLY);
    if (src_fd == -1) {
        perror("open source file");
        return -1;
    }
    
    // Определяем права доступа для целевого файла
    mode_t mode = st.st_mode;
    
    // Если файл исполняемый, сохраняем биты исполнения
    // Для обычных текстовых файлов используем стандартные права 0644
    if (!(st.st_mode & (S_IXUSR | S_IXGRP | S_IXOTH))) {
        mode = 0644; // Обычный текстовый файл
    }
    
    // Открываем целевой файл для записи с нужными правами
    dst_fd = open(dst_path, O_WRONLY | O_CREAT | O_TRUNC, mode);
    if (dst_fd == -1) {
        perror("open destination file");
        close(src_fd);
        return -1;
    }
    
    // Читаем и записываем файл по частям
    while ((bytes_read = read(src_fd, buffer, BUFFER_SIZE)) > 0) {
        bytes_written = write(dst_fd, buffer, bytes_read);
        if (bytes_written != bytes_read) {
            perror("write");
            close(src_fd);
            close(dst_fd);
            return -1;
        }
    }
    
    if (bytes_read == -1) {
        perror("read");
        close(src_fd);
        close(dst_fd);
        return -1;
    }
    
    // Закрываем файлы
    close(src_fd);
    close(dst_fd);
    
    return 0;
}

// Функция для определения типа копирования на основе аргументов
int determine_copy_type(int argc, char *argv[]) {
    if (argc == 3) {
        return 1; // Большой буфер
    } else if (argc == 4) {
        if (strcmp(argv[3], "-small") == 0) {
            return 2; // Маленький буфер
        } else if (strcmp(argv[3], "-preserve") == 0) {
            return 3; // Сохранение прав
        }
    }
    return -1; // Ошибка
}

void print_usage(const char *program_name) {
    printf("Использование:\n");
    printf("  %s <исходный_файл> <целевой_файл>           - копирование с большим буфером\n", program_name);
    printf("  %s <исходный_файл> <целевой_файл> -small    - копирование с буфером 32 байта\n", program_name);
    printf("  %s <исходный_файл> <целевой_файл> -preserve - копирование с сохранением прав доступа\n", program_name);
}

int main(int argc, char *argv[]) {
    int copy_type;
    
    // Проверяем аргументы командной строки
    if (argc < 3 || argc > 4) {
        print_usage(argv[0]);
        return 1;
    }
    
    // Определяем тип копирования
    copy_type = determine_copy_type(argc, argv);
    if (copy_type == -1) {
        fprintf(stderr, "Ошибка: неверные аргументы\n");
        print_usage(argv[0]);
        return 1;
    }
    
    const char *src_path = argv[1];
    const char *dst_path = argv[2];
    int result;
    
    // Выполняем копирование в зависимости от выбранного типа
    switch (copy_type) {
        case 1:
            printf("Копирование с большим буфером: %s -> %s\n", src_path, dst_path);
            result = copy_file_large_buffer(src_path, dst_path);
            break;
        case 2:
            printf("Копирование с буфером 32 байта: %s -> %s\n", src_path, dst_path);
            result = copy_file_small_buffer(src_path, dst_path);
            break;
        case 3:
            printf("Копирование с сохранением прав доступа: %s -> %s\n", src_path, dst_path);
            result = copy_file_preserve_permissions(src_path, dst_path);
            break;
        default:
            result = -1;
            break;
    }
    
    if (result == 0) {
        printf("Копирование завершено успешно\n");
    } else {
        fprintf(stderr, "Ошибка при копировании файла\n");
        return 1;
    }
    
    return 0;
}