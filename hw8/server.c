#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <string.h>

#define SHM_NAME "/random_numbers_shm"
#define SHM_SIZE sizeof(struct shared_data)

struct shared_data {
    int number;
    int client_ready;
    int server_ready;
};

volatile sig_atomic_t keep_running = 1;

void signal_handler(int sig) {
    keep_running = 0;
}

int main() {
    // Установка обработчиков сигналов
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);

    // Создание сегмента разделяемой памяти
    int shm_fd = shm_open(SHM_NAME, O_CREAT | O_RDWR | O_EXCL, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }

    // Установка размера сегмента
    if (ftruncate(shm_fd, SHM_SIZE) == -1) {
        perror("ftruncate");
        shm_unlink(SHM_NAME);
        exit(1);
    }

    // Отображение в память
    struct shared_data *data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        shm_unlink(SHM_NAME);
        exit(1);
    }

    // Инициализация разделяемой памяти
    data->number = 0;
    data->client_ready = 0;
    data->server_ready = 1;

    printf("Server started. PID: %d\n", getpid());
    printf("Shared memory created. Waiting for client...\n");
    printf("Press Ctrl+C to stop\n");

    int last_number = -1;
    
    while (keep_running) {
        if (data->client_ready) {
            if (data->number != last_number) {
                printf("Received number: %d\n", data->number);
                last_number = data->number;
            }
            data->client_ready = 0;
            data->server_ready = 1;
        }
        usleep(100000); 
    }

    // Корректное завершение
    printf("\nShutting down server...\n");
    
    munmap(data, SHM_SIZE);
    close(shm_fd);
    shm_unlink(SHM_NAME);
    
    printf("Server stopped. Shared memory cleaned up.\n");
    return 0;
}