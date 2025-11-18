// common.cpp
#include "common.h"

// Определение имен семафоров
const char* SEM_GALLERY_ENTRY = "/gallery_entry";
const char* SEM_GALLERY_EXIT = "/gallery_exit";
const char* SEM_PAINTING_BASE = "/painting_";
const char* SEM_WATCHER = "/watcher_sem";
const char* SEM_OBSERVER = "/observer_sem";
const char* SEM_MESSAGE = "/message_sem";

// Определение имен разделяемой памяти
const char* SHM_GALLERY_STATE = "/gallery_state";
const char* SHM_MESSAGES = "/gallery_messages";

void cleanup_resources() {
    std::cout << "Очистка ресурсов..." << std::endl;
    
    // Закрываем и удаляем семафоры
    sem_unlink(SEM_GALLERY_ENTRY);
    sem_unlink(SEM_GALLERY_EXIT);
    sem_unlink(SEM_WATCHER);
    sem_unlink(SEM_OBSERVER);
    sem_unlink(SEM_MESSAGE);
    
    for (int i = 0; i < NUM_PAINTINGS; i++) {
        std::string sem_name = std::string(SEM_PAINTING_BASE) + std::to_string(i);
        sem_unlink(sem_name.c_str());
    }
    
    // Удаляем разделяемую память
    shm_unlink(SHM_GALLERY_STATE);
    shm_unlink(SHM_MESSAGES);
    
    std::cout << "Ресурсы очищены." << std::endl;
}

void signal_handler(int sig) {
    std::cout << "\nПолучен сигнал " << sig << ", завершение..." << std::endl;
    cleanup_resources();
    exit(0);
}

void send_message(MessageBuffer* buffer, sem_t* sem_message, const ObserverMessage& msg) {
    sem_wait(sem_message);
    
    buffer->messages[buffer->write_index] = msg;
    buffer->write_index = (buffer->write_index + 1) % MESSAGE_BUFFER_SIZE;
    
    sem_post(sem_message);
}

bool receive_message(MessageBuffer* buffer, sem_t* sem_message, ObserverMessage& msg) {
    sem_wait(sem_message);
    
    if (buffer->read_index == buffer->write_index) {
        sem_post(sem_message);
        return false; // Нет новых сообщений
    }
    
    msg = buffer->messages[buffer->read_index];
    buffer->read_index = (buffer->read_index + 1) % MESSAGE_BUFFER_SIZE;
    
    sem_post(sem_message);
    return true;
}