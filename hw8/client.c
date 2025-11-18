#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/mman.h>
#include <signal.h>
#include <time.h>

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

    // Открытие существующего сегмента разделяемой памяти
    int shm_fd = shm_open(SHM_NAME, O_RDWR, 0666);
    if (shm_fd == -1) {
        perror("shm_open");
        exit(1);
    }

    // Отображение в память
    struct shared_data *data = mmap(NULL, SHM_SIZE, PROT_READ | PROT_WRITE, 
                                   MAP_SHARED, shm_fd, 0);
    if (data == MAP_FAILED) {
        perror("mmap");
        close(shm_fd);
        exit(1);
    }

    printf("Client started. PID: %d\n", getpid());
    printf("Press Ctrl+C to stop\n");

    srand(time(NULL));
    
    while (keep_running) {
        if (data->server_ready) {
            // Генерация случайного числа от 0 до 999
            data->number = rand() % 1000;
            data->client_ready = 1;
            data->server_ready = 0;
            
            // Небольшая задержка для регулирования скорости
            usleep(500000);
        }
    }

    // Корректное завершение
    printf("\nShutting down client...\n");
    
    munmap(data, SHM_SIZE);
    close(shm_fd);
    
    printf("Client stopped.\n");
    return 0;
}