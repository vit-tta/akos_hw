// common.h
#ifndef COMMON_H
#define COMMON_H

#include <iostream>
#include <fcntl.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <unistd.h>
#include <cstring>
#include <cstdlib>
#include <csignal>
#include <ctime>
#include <random>
#include <string>
#include <sys/stat.h>

// Константы через define для использования в размерах массивов
#define MAX_VISITORS 25
#define MAX_QUEUE 10
#define NUM_PAINTINGS 5
#define MAX_PER_PAINTING 5
#define TOTAL_VISITORS 150
#define SIMULATION_TIME 30
#define MAX_OBSERVERS 3
#define MESSAGE_BUFFER_SIZE 100

// Имена семафоров
extern const char* SEM_GALLERY_ENTRY;
extern const char* SEM_GALLERY_EXIT;
extern const char* SEM_PAINTING_BASE;
extern const char* SEM_WATCHER;
extern const char* SEM_OBSERVER;
extern const char* SEM_MESSAGE;

// Имена разделяемой памяти
extern const char* SHM_GALLERY_STATE;
extern const char* SHM_MESSAGES;

// Структуры данных
struct GalleryState {
    int current_visitors;
    int painting_counts[NUM_PAINTINGS];
    int total_served;
    int waiting_visitors;
    bool simulation_active;
    int message_count;
};

struct ObserverMessage {
    int visitor_id;
    int action; // 0-вошел, 1-вышел, 2-у картины, 3-ожидание
    int painting_id;
    char message[256];
};

struct MessageBuffer {
    ObserverMessage messages[MESSAGE_BUFFER_SIZE];
    int read_index;
    int write_index;
    int observer_count;
};

// Прототипы функций
void cleanup_resources();
void signal_handler(int sig);
void send_message(MessageBuffer* buffer, sem_t* sem_message, const ObserverMessage& msg);
bool receive_message(MessageBuffer* buffer, sem_t* sem_message, ObserverMessage& msg);

#endif