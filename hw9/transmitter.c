#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <signal.h>
#include <string.h>

volatile sig_atomic_t ack_received = 0;
volatile sig_atomic_t receiver_pid = 0;

void ack_handler(int sig) {
    ack_received = 1;
}

void send_bit(int bit, int receiver_pid) {
    ack_received = 0;
    
    if (bit == 0) {
        kill(receiver_pid, SIGUSR1);
    } else {
        kill(receiver_pid, SIGUSR2);
    }
    
    // Ждем подтверждения
    while (!ack_received) {
        pause();
    }
}

int main() {
    printf("Transmitter PID: %d\n", getpid());
    
    // Установка обработчика для подтверждения
    struct sigaction sa;
    sa.sa_handler = ack_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    
    if (sigaction(SIGUSR1, &sa, NULL) == -1) {
        perror("sigaction");
        exit(1);
    }
    
    // Запрос PID приемника
    printf("Enter receiver PID: ");
    scanf("%d", &receiver_pid);
    
    // Запрос числа для передачи
    int number;
    printf("Enter integer number to transmit: ");
    scanf("%d", &number);
    
    printf("Transmitting number: %d\n", number);
    
    // Преобразуем число в битовое представление
    unsigned int unsigned_num = (unsigned int)number;
    
    // Передаем биты (32 бита для int)
    for (int i = 0; i < 32; i++) {
        int bit = (unsigned_num >> i) & 1;
        send_bit(bit, receiver_pid);
    }
    
    // Сигнал завершения передачи
    kill(receiver_pid, SIGINT);
    
    printf("Transmission completed!\n");
    
    return 0;
}