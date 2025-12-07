#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <stdatomic.h>

// Структуры для хранения данных
typedef struct {
    int id;
    bool in_gallery;
    int* visit_order;
    pthread_t thread;
} Visitor;

typedef struct {
    int id;
    atomic_int current_visitors;
    pthread_mutex_t mutex;
    pthread_cond_t cond;  // Условная переменная для ожидания
} Painting;

// Глобальные переменные
int TOTAL_VISITORS;
int MAX_IN_GALLERY = 25;
int NUM_PAINTINGS = 5;
int MAX_PER_PAINTING = 5;

Visitor* visitors;
Painting* paintings;

atomic_int visitors_in_gallery;
atomic_int visitors_completed;
atomic_bool running;
pthread_mutex_t gallery_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t entry_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t entry_cond = PTHREAD_COND_INITIALIZER;

FILE* output_file = NULL;

// Функция для вывода с временной меткой
void print_message(const char* message) {
    time_t now;
    time(&now);
    struct tm* local = localtime(&now);
    char timestamp[20];
    strftime(timestamp, sizeof(timestamp), "%H:%M:%S", local);
    
    pthread_mutex_lock(&gallery_mutex);
    printf("[%s] %s\n", timestamp, message);
    
    if (output_file) {
        fprintf(output_file, "[%s] %s\n", timestamp, message);
        fflush(output_file);
    }
    
    pthread_mutex_unlock(&gallery_mutex);
}

// Функция для входа в галерею с использованием условной переменной
bool enter_gallery(int visitor_id) {
    pthread_mutex_lock(&entry_mutex);
    
    char message[256];
    
    while (atomic_load(&visitors_in_gallery) >= MAX_IN_GALLERY) {
        sprintf(message, "Посетитель %d ждет - галерея полная (%d/%d)", 
                visitor_id, atomic_load(&visitors_in_gallery), MAX_IN_GALLERY);
        print_message(message);
        
        // Ожидаем сигнала, что появилось место
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // Таймаут 1 секунда
        
        if (pthread_cond_timedwait(&entry_cond, &entry_mutex, &ts) != 0) {
            // Таймаут - проверяем снова условие
            if (!atomic_load(&running)) {
                pthread_mutex_unlock(&entry_mutex);
                return false;
            }
        }
    }
    
    // Входим в галерею
    atomic_fetch_add(&visitors_in_gallery, 1);
    pthread_mutex_unlock(&entry_mutex);
    
    return true;
}

// Функция для выхода из галереи
void exit_gallery(int visitor_id) {
    pthread_mutex_lock(&entry_mutex);
    atomic_fetch_sub(&visitors_in_gallery, 1);
    
    // Сигнализируем ожидающим, что появилось место
    pthread_cond_broadcast(&entry_cond);
    pthread_mutex_unlock(&entry_mutex);
}

// Функция для подхода к картине с использованием условной переменной
bool approach_painting(int visitor_id, int painting_id) {
    Painting* painting = &paintings[painting_id];
    
    pthread_mutex_lock(&painting->mutex);
    
    // Ждем, пока не будет свободного места
    while (atomic_load(&painting->current_visitors) >= MAX_PER_PAINTING) {
        char message[256];
        sprintf(message, "Посетитель %d ждет у картины %d (%d/%d)", 
                visitor_id, painting_id + 1, 
                atomic_load(&painting->current_visitors), MAX_PER_PAINTING);
        print_message(message);
        
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1; // Таймаут 1 секунда
        
        if (pthread_cond_timedwait(&painting->cond, &painting->mutex, &ts) != 0) {
            if (!atomic_load(&running)) {
                pthread_mutex_unlock(&painting->mutex);
                return false;
            }
        }
    }
    
    // Подходим к картине
    atomic_fetch_add(&painting->current_visitors, 1);
    pthread_mutex_unlock(&painting->mutex);
    
    return true;
}

// Функция для отхода от картины
void leave_painting(int visitor_id, int painting_id) {
    Painting* painting = &paintings[painting_id];
    
    pthread_mutex_lock(&painting->mutex);
    atomic_fetch_sub(&painting->current_visitors, 1);
    
    // Сигнализируем ожидающим
    pthread_cond_broadcast(&painting->cond);
    pthread_mutex_unlock(&painting->mutex);
}

// Функция посетителя
void* visitor_thread(void* arg) {
    Visitor* visitor = (Visitor*)arg;
    char message[256];
    
    sprintf(message, "Посетитель %d ждет у входа", visitor->id);
    print_message(message);
    
    // Пытаемся войти в галерею
    if (!enter_gallery(visitor->id)) {
        sprintf(message, "Посетитель %d не смог войти (галерея закрывается)", visitor->id);
        print_message(message);
        return NULL;
    }
    
    visitor->in_gallery = true;
    sprintf(message, "Посетитель %d вошел в галерею. В галерее: %d/%d", 
            visitor->id, atomic_load(&visitors_in_gallery), MAX_IN_GALLERY);
    print_message(message);
    
    // Осматриваем картины
    for (int i = 0; i < NUM_PAINTINGS && atomic_load(&running); i++) {
        int painting_id = visitor->visit_order[i];
        
        sprintf(message, "Посетитель %d хочет посмотреть картину %d", 
                visitor->id, painting_id + 1);
        print_message(message);
        
        // Подходим к картине
        if (!approach_painting(visitor->id, painting_id)) {
            sprintf(message, "Посетитель %d прервал осмотр", visitor->id);
            print_message(message);
            break;
        }
        
        sprintf(message, "Посетитель %d смотрит картину %d. У картины: %d/%d", 
                visitor->id, painting_id + 1, 
                atomic_load(&paintings[painting_id].current_visitors), MAX_PER_PAINTING);
        print_message(message);
        
        // Время осмотра
        int view_time = 1 + rand() % 3;
        sleep(view_time);
        
        // Отходим от картины
        leave_painting(visitor->id, painting_id);
        
        sprintf(message, "Посетитель %d отошел от картины %d. У картины: %d/%d", 
                visitor->id, painting_id + 1, 
                atomic_load(&paintings[painting_id].current_visitors), MAX_PER_PAINTING);
        print_message(message);
    }
    
    // Покидаем галерею
    exit_gallery(visitor->id);
    atomic_fetch_add(&visitors_completed, 1);
    
    sprintf(message, "Посетитель %d покинул галерею. Завершено: %d/%d, В галерее: %d/%d", 
            visitor->id, atomic_load(&visitors_completed), TOTAL_VISITORS, 
            atomic_load(&visitors_in_gallery), MAX_IN_GALLERY);
    print_message(message);
    
    return NULL;
}

// Функция вахтера
void* guard_thread(void* arg) {
    char message[256];
    
    while (atomic_load(&running)) {
        sprintf(message, "ВАХТЕР: В галерее %d/%d посетителей, завершили %d/%d", 
                atomic_load(&visitors_in_gallery), MAX_IN_GALLERY, 
                atomic_load(&visitors_completed), TOTAL_VISITORS);
        print_message(message);
        
        // Информация по картинам
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            sprintf(message, "Картина %d: %d/%d посетителей", 
                    i + 1, atomic_load(&paintings[i].current_visitors), MAX_PER_PAINTING);
            print_message(message);
        }
        
        sleep(2);
        
        if (atomic_load(&visitors_completed) >= TOTAL_VISITORS) {
            print_message("ВАХТЕР: Все посетители обслужены, галерея закрывается");
            atomic_store(&running, false);
            
            // Будим всех ожидающих
            pthread_mutex_lock(&entry_mutex);
            pthread_cond_broadcast(&entry_cond);
            pthread_mutex_unlock(&entry_mutex);
            
            for (int i = 0; i < NUM_PAINTINGS; i++) {
                pthread_mutex_lock(&paintings[i].mutex);
                pthread_cond_broadcast(&paintings[i].cond);
                pthread_mutex_unlock(&paintings[i].mutex);
            }
            
            break;
        }
    }
    
    return NULL;
}

// Обработчик сигналов
void signal_handler(int sig) {
    if (sig == SIGINT) {
        print_message("Получен сигнал SIGINT. Завершение работы...");
        atomic_store(&running, false);
        
        // Будим все ожидающие потоки
        pthread_mutex_lock(&entry_mutex);
        pthread_cond_broadcast(&entry_cond);
        pthread_mutex_unlock(&entry_mutex);
        
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            pthread_mutex_lock(&paintings[i].mutex);
            pthread_cond_broadcast(&paintings[i].cond);
            pthread_mutex_unlock(&paintings[i].mutex);
        }
    }
}

// Генерация случайного порядка осмотра
int* generate_visit_order() {
    int* order = malloc(NUM_PAINTINGS * sizeof(int));
    for (int i = 0; i < NUM_PAINTINGS; i++) {
        order[i] = i;
    }
    
    for (int i = NUM_PAINTINGS - 1; i > 0; i--) {
        int j = rand() % (i + 1);
        int temp = order[i];
        order[i] = order[j];
        order[j] = temp;
    }
    
    return order;
}

int main(int argc, char* argv[]) {
    // Обработка аргументов командной строки
    if (argc < 3) {
        printf("Использование: %s <общее_количество_посетителей> <выходной_файл> [конфигурационный_файл]\n", argv[0]);
        printf("Или: %s -f <конфигурационный_файл> <выходной_файл>\n", argv[0]);
        return 1;
    }
    
    // Установка обработчика сигналов
    signal(SIGINT, signal_handler);
    
    // Инициализация
    srand(time(NULL));
    atomic_store(&visitors_in_gallery, 0);
    atomic_store(&visitors_completed, 0);
    atomic_store(&running, true);
    
    // Обработка конфигурационного файла
    if (strcmp(argv[1], "-f") == 0 && argc >= 4) {
        FILE* config = fopen(argv[2], "r");
        if (!config) {
            printf("Ошибка открытия конфигурационного файла: %s\n", argv[2]);
            return 1;
        }
        
        fscanf(config, "%d", &TOTAL_VISITORS);
        fscanf(config, "%d", &MAX_IN_GALLERY);
        fscanf(config, "%d", &NUM_PAINTINGS);
        fscanf(config, "%d", &MAX_PER_PAINTING);
        
        fclose(config);
        
        output_file = fopen(argv[3], "w");
        if (!output_file) {
            printf("Ошибка открытия выходного файла: %s\n", argv[3]);
            return 1;
        }
    } else {
        TOTAL_VISITORS = atoi(argv[1]);
        if (TOTAL_VISITORS < 100 || TOTAL_VISITORS > 300) {
            printf("Количество посетителей должно быть от 100 до 300\n");
            return 1;
        }
        
        output_file = fopen(argv[2], "w");
        if (!output_file) {
            printf("Ошибка открытия выходного файла: %s\n", argv[2]);
            return 1;
        }
    }
    
    printf("=== КАРТИННАЯ ГАЛЕРЕЯ (Условные переменные) ===\n");
    printf("Всего посетителей за день: %d\n", TOTAL_VISITORS);
    printf("Максимум в галерее: %d\n", MAX_IN_GALLERY);
    printf("Количество картин: %d\n", NUM_PAINTINGS);
    printf("Максимум у каждой картины: %d\n\n", MAX_PER_PAINTING);
    
    // Инициализация условных переменных
    pthread_mutex_init(&entry_mutex, NULL);
    pthread_cond_init(&entry_cond, NULL);
    
    // Инициализация картин
    paintings = malloc(NUM_PAINTINGS * sizeof(Painting));
    for (int i = 0; i < NUM_PAINTINGS; i++) {
        paintings[i].id = i;
        atomic_init(&paintings[i].current_visitors, 0);
        pthread_mutex_init(&paintings[i].mutex, NULL);
        pthread_cond_init(&paintings[i].cond, NULL);
    }
    
    // Создание посетителей
    visitors = malloc(TOTAL_VISITORS * sizeof(Visitor));
    
    // Создание потока вахтера
    pthread_t guard;
    pthread_create(&guard, NULL, guard_thread, NULL);
    
    // Запуск посетителей
    for (int i = 0; i < TOTAL_VISITORS && atomic_load(&running); i++) {
        visitors[i].id = i + 1;
        visitors[i].in_gallery = false;
        visitors[i].visit_order = generate_visit_order();
        
        pthread_create(&visitors[i].thread, NULL, visitor_thread, &visitors[i]);
        
        usleep(100000 + rand() % 400000);
    }
    
    // Ожидание завершения
    for (int i = 0; i < TOTAL_VISITORS; i++) {
        pthread_join(visitors[i].thread, NULL);
        free(visitors[i].visit_order);
    }
    
    pthread_join(guard, NULL);
    
    // Освобождение ресурсов
    for (int i = 0; i < NUM_PAINTINGS; i++) {
        pthread_mutex_destroy(&paintings[i].mutex);
        pthread_cond_destroy(&paintings[i].cond);
    }
    
    pthread_mutex_destroy(&entry_mutex);
    pthread_cond_destroy(&entry_cond);
    free(paintings);
    free(visitors);
    
    if (output_file) {
        fclose(output_file);
    }
    
    printf("\n=== РАБОТА ГАЛЕРЕИ ЗАВЕРШЕНА ===\n");
    printf("Всего посетителей: %d\n", TOTAL_VISITORS);
    printf("Успешно обслужено: %d\n", atomic_load(&visitors_completed));
    
    return 0;
}