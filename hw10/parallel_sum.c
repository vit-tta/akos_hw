#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <time.h>
#include <stdbool.h>
#include <string.h>
#include <signal.h>
#include <errno.h>

#define NUM_SOURCES 100
#define BUFFER_SIZE 1000
#define MAX_VALUE 100

typedef struct {
    int value;
    int source_id;
    bool used;
} BufferItem;

typedef struct {
    int id;
    pthread_t thread;
} Source;

typedef struct {
    int id;
    int a, b;
    int source_a, source_b;
    pthread_t thread;
} Summator;

BufferItem buffer[BUFFER_SIZE];
int buffer_count = 0;
int generated_count = 0;
int summator_counter = 0;
int active_summators = 0;
int completed_summations = 0;

pthread_mutex_t buffer_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t output_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t data_available = PTHREAD_COND_INITIALIZER;

volatile bool running = true;
volatile bool done = false;
int final_result = 0;

void print_msg(const char* msg) {
    time_t now = time(NULL);
    struct tm* t = localtime(&now);
    
    pthread_mutex_lock(&output_mutex);
    printf("[%02d:%02d:%02d] %s\n", t->tm_hour, t->tm_min, t->tm_sec, msg);
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);
}

void print_status() {
    pthread_mutex_lock(&output_mutex);
    printf("\n=== СТАТУС ===\n");
    printf("Сгенерировано: %d/%d\n", generated_count, NUM_SOURCES);
    printf("В буфере: %d элементов\n", buffer_count);
    
    int available = 0;
    for (int i = 0; i < buffer_count; i++) {
        if (!buffer[i].used) available++;
    }
    printf("Доступно для суммирования: %d\n", available);
    printf("Активных сумматоров: %d\n", active_summators);
    printf("Выполнено суммирований: %d\n", completed_summations);
    printf("================\n\n");
    fflush(stdout);
    pthread_mutex_unlock(&output_mutex);
}

// Добавить число в буфер
void add_to_buffer(int value, int source_id) {
    pthread_mutex_lock(&buffer_mutex);
    
    if (buffer_count < BUFFER_SIZE) {
        buffer[buffer_count].value = value;
        buffer[buffer_count].source_id = source_id;
        buffer[buffer_count].used = false;
        buffer_count++;
        
        // Сигнализируем о новых данных
        pthread_cond_signal(&data_available);
    }
    
    pthread_mutex_unlock(&buffer_mutex);
}

// Найти два неиспользованных числа
bool find_pair(int* idx1, int* idx2) {
    *idx1 = -1;
    *idx2 = -1;
    
    for (int i = 0; i < buffer_count; i++) {
        if (!buffer[i].used) {
            if (*idx1 == -1) {
                *idx1 = i;
            } else {
                *idx2 = i;
                return true;
            }
        }
    }
    return false;
}

// Пометить числа как использованные
void mark_used(int idx1, int idx2) {
    if (idx1 >= 0 && idx1 < buffer_count) buffer[idx1].used = true;
    if (idx2 >= 0 && idx2 < buffer_count) buffer[idx2].used = true;
}

// Убрать использованные числа из буфера
void cleanup_buffer() {
    int new_count = 0;
    for (int i = 0; i < buffer_count; i++) {
        if (!buffer[i].used) {
            if (i != new_count) {
                buffer[new_count] = buffer[i];
            }
            new_count++;
        }
    }
    buffer_count = new_count;
}

// Проверить завершение
bool check_completion() {
    // Если все сгенерировано и в буфере один элемент
    if (generated_count >= NUM_SOURCES && buffer_count == 1 && !buffer[0].used) {
        final_result = buffer[0].value;
        
        char msg[100];
        if (buffer[0].source_id >= 0) {
            sprintf(msg, "РЕЗУЛЬТАТ: Число %d от источника %d", 
                    final_result, buffer[0].source_id);
        } else {
            sprintf(msg, "РЕЗУЛЬТАТ: Сумма = %d", final_result);
        }
        print_msg(msg);
        
        done = true;
        return true;
    }
    
    // Если все сгенерировано, нет активных сумматоров и нельзя найти пару
    if (generated_count >= NUM_SOURCES && active_summators == 0) {
        int idx1, idx2;
        if (!find_pair(&idx1, &idx2)) {
            if (buffer_count == 1) {
                final_result = buffer[0].value;
                char msg[100];
                sprintf(msg, "РЕЗУЛЬТАТ: Остался один элемент: %d", final_result);
                print_msg(msg);
                done = true;
                return true;
            } else if (buffer_count == 0) {
                print_msg("ОШИБКА: Буфер пуст, но вычисления не завершены!");
                done = true;
                return true;
            }
        }
    }
    
    return false;
}

void* source_thread(void* arg) {
    Source* src = (Source*)arg;
    
    // Случайная задержка 1-7 секунд
    int delay = 1 + rand() % 7;
    sleep(delay);
    
    // Случайное число 1-100
    int number = 1 + rand() % MAX_VALUE;
    
    // Добавляем в буфер
    add_to_buffer(number, src->id);
    
    // Логируем
    char msg[100];
    sprintf(msg, "Источник %d: число %d (задержка %d сек)", 
            src->id, number, delay);
    print_msg(msg);
    
    pthread_mutex_lock(&buffer_mutex);
    generated_count++;
    pthread_mutex_unlock(&buffer_mutex);
    
    return NULL;
}

void* summator_thread(void* arg) {
    Summator* sum = (Summator*)arg;
    
    // Логируем начало
    char msg[100];
    sprintf(msg, "Сумматор %d: начал %d + %d", 
            sum->id, sum->a, sum->b);
    print_msg(msg);
    
    // Время работы 3-6 секунд
    int work_time = 3 + rand() % 4;
    sleep(work_time);
    
    // Суммируем
    int result = sum->a + sum->b;
    
    // Логируем результат
    sprintf(msg, "Сумматор %d: результат %d (время %d сек)", 
            sum->id, result, work_time);
    print_msg(msg);
    
    // Добавляем результат обратно в буфер
    add_to_buffer(result, -1);
    
    // Обновляем статистику
    pthread_mutex_lock(&buffer_mutex);
    active_summators--;
    completed_summations++;
    pthread_mutex_unlock(&buffer_mutex);
    
    free(sum);
    return NULL;
}

void* monitor_thread(void* arg) {
    print_msg("МОНИТОР: Запущен");
    
    while (running && !done) {
        pthread_mutex_lock(&buffer_mutex);
        
        // Ждем, пока не появится хотя бы 2 доступных числа
        while (running) {
            // Проверяем завершение
            if (check_completion()) {
                pthread_mutex_unlock(&buffer_mutex);
                return NULL;
            }
            
            // Ищем пару
            int idx1, idx2;
            if (find_pair(&idx1, &idx2)) {
                break; // Нашли пару
            }
            
            // Если не нашли - ждем
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1; // Таймаут 1 сек
            
            pthread_cond_timedwait(&data_available, &buffer_mutex, &ts);
        }
        
        if (!running || done) {
            pthread_mutex_unlock(&buffer_mutex);
            return NULL;
        }
        
        // Нашли пару - создаем сумматор
        int idx1, idx2;
        find_pair(&idx1, &idx2); // Должно сработать, т.к. мы только что проверяли
        
        Summator* sum = malloc(sizeof(Summator));
        sum->id = ++summator_counter;
        sum->a = buffer[idx1].value;
        sum->b = buffer[idx2].value;
        sum->source_a = buffer[idx1].source_id;
        sum->source_b = buffer[idx2].source_id;
        
        // Помечаем как использованные
        mark_used(idx1, idx2);
        
        // Увеличиваем счетчик активных сумматоров
        active_summators++;
        
        pthread_mutex_unlock(&buffer_mutex);
        
        // Создаем поток сумматора
        if (pthread_create(&sum->thread, NULL, summator_thread, sum) != 0) {
            print_msg("ОШИБКА создания сумматора!");
            free(sum);
            continue;
        }
        
        // Отсоединяем поток
        pthread_detach(sum->thread);
        
        // Логируем
        char msg[100];
        sprintf(msg, "МОНИТОР: создал сумматор %d для %d + %d", 
                sum->id, sum->a, sum->b);
        print_msg(msg);
        
        // Периодически чистим буфер
        if (summator_counter % 5 == 0) {
            pthread_mutex_lock(&buffer_mutex);
            cleanup_buffer();
            pthread_mutex_unlock(&buffer_mutex);
        }
        
        // Небольшая пауза
        usleep(100000);
    }
    
    print_msg("МОНИТОР: Завершен");
    return NULL;
}

void* status_thread(void* arg) {
    while (running && !done) {
        sleep(2);
        if (!running || done) break;
        print_status();
    }
    return NULL;
}

void handle_signal(int sig) {
    if (sig == SIGINT) {
        print_msg("Получен SIGINT. Завершение...");
        running = false;
        
        // Будим все ждущие потоки
        pthread_mutex_lock(&buffer_mutex);
        pthread_cond_broadcast(&data_available);
        pthread_mutex_unlock(&buffer_mutex);
    }
}

int main() {
    printf("   ПАРАЛЛЕЛЬНОЕ СУММИРОВАНИЕ 100 чисел\n");
    
    // Настройка обработки сигналов
    signal(SIGINT, handle_signal);
    
    // Инициализация генератора случайных чисел
    srand(time(NULL));
    
    // Создание источников
    print_msg("Создание источников...");
    Source sources[NUM_SOURCES];
    
    for (int i = 0; i < NUM_SOURCES; i++) {
        sources[i].id = i;
        if (pthread_create(&sources[i].thread, NULL, source_thread, &sources[i]) != 0) {
            fprintf(stderr, "Ошибка создания источника %d\n", i);
        }
        usleep(10000); // Небольшая задержка
    }
    
    // Создание монитора
    print_msg("Запуск монитора...");
    pthread_t monitor;
    pthread_create(&monitor, NULL, monitor_thread, NULL);
    
    // Создание потока статуса
    print_msg("Запуск потока статуса...");
    pthread_t status;
    pthread_create(&status, NULL, status_thread, NULL);
    
    // Ожидание завершения источников
    print_msg("Ожидание завершения источников...");
    for (int i = 0; i < NUM_SOURCES; i++) {
        pthread_join(sources[i].thread, NULL);
    }
    print_msg("Все источники завершены");
    
    // Ожидание завершения вычислений
    print_msg("Ожидание завершения вычислений...");
    
    // Даем время на завершение
    for (int i = 0; i < 30 && !done; i++) {
        sleep(1);
        
        pthread_mutex_lock(&buffer_mutex);
        check_completion();
        pthread_mutex_unlock(&buffer_mutex);
        
        if (i % 5 == 0) {
            print_status();
        }
    }
    
    // Завершаем работу
    running = false;
    pthread_cond_broadcast(&data_available);
    
    // Ожидаем завершения монитора и статуса
    pthread_join(monitor, NULL);
    pthread_join(status, NULL);
    
    // Финальный вывод
    printf("               ИТОГИ\n");
    
    if (done) {
        printf("УСПЕХ: Вычисления завершены!\n");
        printf("Результат: %d\n", final_result);
    } else {
        printf("ВЫЧИСЛЕНИЯ НЕ ЗАВЕРШЕНЫ ПОЛНОСТЬЮ\n");
        printf("Сгенерировано чисел: %d/%d\n", generated_count, NUM_SOURCES);
        printf("Осталось в буфере: %d элементов\n", buffer_count);
        printf("Активных сумматоров: %d\n", active_summators);
    }
    
    printf("\nСтатистика:\n");
    printf("  Сгенерировано чисел: %d\n", generated_count);
    printf("  Выполнено суммирований: %d\n", completed_summations);
    printf("  Создано сумматоров: %d\n", summator_counter);
    
    // Очистка
    pthread_mutex_destroy(&buffer_mutex);
    pthread_mutex_destroy(&output_mutex);
    pthread_cond_destroy(&data_available);
    
    printf("\nПрограмма завершена.\n");
    return 0;
}