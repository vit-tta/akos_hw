#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

volatile sig_atomic_t received_bits[32];
volatile sig_atomic_t bit_count = 0;
volatile sig_atomic_t transmitter_pid = 0;
volatile sig_atomic_t transmission_complete = 0;

void bit_handler(int sig, siginfo_t *info, void *context) {
    if (info->si_pid != transmitter_pid) {
        return; // Игнорируем сигналы от других процессов
    }
    
    if (sig == SIGUSR1) {
        received_bits[bit_count] = 0;
    } else if (sig == SIGUSR2) {
        received_bits[bit_count] = 1;
    }
    
    bit_count++;
    
    // Отправляем подтверждение
    kill(transmitter_pid, SIGUSR1);
}

void completion_handler(int sig, siginfo_t *info, void *context) {
    if (info->si_pid == transmitter_pid) {
        transmission_complete = 1;
    }
}

int main() {
    printf("Receiver PID: %d\n", getpid());
    
    // Установка обработчиков сигналов
    struct sigaction sa_bit, sa_complete;
    
    // Обработчик для битов
    sa_bit.sa_sigaction = bit_handler;
    sigemptyset(&sa_bit.sa_mask);
    sa_bit.sa_flags = SA_SIGINFO;
    
    // Обработчик для завершения передачи
    sa_complete.sa_sigaction = completion_handler;
    sigemptyset(&sa_complete.sa_mask);
    sa_complete.sa_flags = SA_SIGINFO;
    
    if (sigaction(SIGUSR1, &sa_bit, NULL) == -1 ||
        sigaction(SIGUSR2, &sa_bit, NULL) == -1 ||
        sigaction(SIGINT, &sa_complete, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Запрос PID передатчика
    printf("Enter transmitter PID: ");
    scanf("%d", &transmitter_pid);
    
    printf("Waiting for transmission...\n");
    
    // Ожидаем завершения передачи
    while (!transmission_complete) {
        pause();
    }
    
    // Восстанавливаем число из битов
    unsigned int received_num = 0;
    for (int i = 0; i < 32; i++) {
        if (received_bits[i]) {
            received_num |= (1u << i);
        }
    }
    
    // Преобразуем обратно в signed int
    int final_number = (int)received_num;
    
    printf("Received number: %d\n", final_number);
    
    return 0;
}