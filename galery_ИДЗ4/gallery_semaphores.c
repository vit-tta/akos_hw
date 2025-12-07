#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <semaphore.h>
#include <unistd.h>
#include <time.h>
#include <string.h>
#include <signal.h>
#include <stdbool.h>
#include <errno.h>

// Структуры для хранения данных
typedef struct {
    int id;
    bool in_gallery;
    int* visit_order;
    pthread_t thread;
} Visitor;

typedef struct {
    int id;
    int current_visitors;
    sem_t semaphore;
} Painting;

// Глобальные переменные
int TOTAL_VISITORS;
int MAX_IN_GALLERY = 25;
int NUM_PAINTINGS = 5;
int MAX_PER_PAINTING = 5;

Visitor* visitors;
Painting* paintings;

sem_t gallery_semaphore;
pthread_mutex_t gallery_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t exit_mutex = PTHREAD_MUTEX_INITIALIZER;

volatile sig_atomic_t visitors_in_gallery = 0;
volatile sig_atomic_t visitors_completed = 0;
volatile sig_atomic_t running = 1;
volatile sig_atomic_t interrupt_requested = 0;

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

// Обработчик сигналов
void signal_handler(int sig) {
    if (sig == SIGINT) {
        print_message("Получен сигнал SIGINT. Завершение работы...");
        interrupt_requested = 1;
        running = 0;
    }
}

// Поток-вахтер
void* guard_thread(void* arg) {
    char message[256];
    
    while (running) {
        sprintf(message, "ВАХТЕР: В галерее %d/%d посетителей, завершили %d/%d", 
                visitors_in_gallery, MAX_IN_GALLERY, visitors_completed, TOTAL_VISITORS);
        print_message(message);
        
        // Информация по картинам
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            sprintf(message, "Картина %d: %d/%d посетителей", 
                    i + 1, paintings[i].current_visitors, MAX_PER_PAINTING);
            print_message(message);
        }
        
        sleep(2);
        
        if (visitors_completed >= TOTAL_VISITORS) {
            print_message("ВАХТЕР: Все посетители обслужены, галерея закрывается");
            running = 0;
            break;
        }
        
        if (interrupt_requested) {
            print_message("ВАХТЕР: Прерывание запрошено, завершаем работу");
            break;
        }
    }
    
    return NULL;
}

// Функция посетителя с проверкой прерывания
void* visitor_thread(void* arg) {
    Visitor* visitor = (Visitor*)arg;
    char message[256];
    
    // Проверяем прерывание перед началом
    if (interrupt_requested) {
        sprintf(message, "Посетитель %d не начал - галерея закрывается", visitor->id);
        print_message(message);
        return NULL;
    }
    
    sprintf(message, "Посетитель %d ждет у входа", visitor->id);
    print_message(message);
    
    // Пытаемся войти с таймаутом для проверки прерывания
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    ts.tv_sec += 1; // Таймаут 1 секунда
    
    while (running) {
        if (sem_timedwait(&gallery_semaphore, &ts) == 0) {
            break; // Успешно вошли
        } else if (errno == ETIMEDOUT) {
            // Таймаут - проверяем прерывание
            if (interrupt_requested) {
                sprintf(message, "Посетитель %d не смог войти - галерея закрывается", visitor->id);
                print_message(message);
                return NULL;
            }
            // Обновляем время ожидания
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1;
        } else {
            // Другая ошибка
            return NULL;
        }
    }
    
    if (!running) return NULL;
    
    pthread_mutex_lock(&gallery_mutex);
    visitors_in_gallery++;
    pthread_mutex_unlock(&gallery_mutex);
    
    visitor->in_gallery = true;
    sprintf(message, "Посетитель %d вошел в галерею. В галерее: %d/%d", 
            visitor->id, visitors_in_gallery, MAX_IN_GALLERY);
    print_message(message);
    
    // Осматриваем картины
    for (int i = 0; i < NUM_PAINTINGS && running; i++) {
        int painting_id = visitor->visit_order[i];
        
        sprintf(message, "Посетитель %d хочет посмотреть картину %d", 
                visitor->id, painting_id + 1);
        print_message(message);
        
        // Ожидание с таймаутом для картины
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 1;
        
        while (running) {
            if (sem_timedwait(&paintings[painting_id].semaphore, &ts) == 0) {
                break;
            } else if (errno == ETIMEDOUT) {
                if (interrupt_requested) {
                    sprintf(message, "Посетитель %d прерывает осмотр", visitor->id);
                    print_message(message);
                    goto exit_gallery;
                }
                clock_gettime(CLOCK_REALTIME, &ts);
                ts.tv_sec += 1;
            } else {
                goto exit_gallery;
            }
        }
        
        if (!running) goto exit_gallery;
        
        pthread_mutex_lock(&gallery_mutex);
        paintings[painting_id].current_visitors++;
        pthread_mutex_unlock(&gallery_mutex);
        
        sprintf(message, "Посетитель %d смотрит картину %d. У картины: %d/%d", 
                visitor->id, painting_id + 1, 
                paintings[painting_id].current_visitors, MAX_PER_PAINTING);
        print_message(message);
        
        // Время осмотра с проверкой прерывания
        int view_time = 1 + rand() % 3;
        for (int t = 0; t < view_time && running; t++) {
            sleep(1);
            if (interrupt_requested) {
                sprintf(message, "Посетитель %d прерывает осмотр картины %d", 
                        visitor->id, painting_id + 1);
                print_message(message);
                break;
            }
        }
        
        if (!running) {
            sem_post(&paintings[painting_id].semaphore);
            goto exit_gallery;
        }
        
        // Отходим от картины
        pthread_mutex_lock(&gallery_mutex);
        paintings[painting_id].current_visitors--;
        pthread_mutex_unlock(&gallery_mutex);
        
        sem_post(&paintings[painting_id].semaphore);
        
        sprintf(message, "Посетитель %d отошел от картины %d. У картины: %d/%d", 
                visitor->id, painting_id + 1, 
                paintings[painting_id].current_visitors, MAX_PER_PAINTING);
        print_message(message);
    }
    
exit_gallery:
    // Покидаем галерею
    if (visitor->in_gallery) {
        pthread_mutex_lock(&gallery_mutex);
        visitors_in_gallery--;
        visitors_completed++;
        pthread_mutex_unlock(&gallery_mutex);
        
        sem_post(&gallery_semaphore);
        
        sprintf(message, "Посетитель %d покинул галерею. Завершено: %d/%d, В галерее: %d/%d", 
                visitor->id, visitors_completed, TOTAL_VISITORS, 
                visitors_in_gallery, MAX_IN_GALLERY);
        print_message(message);
    } else {
        pthread_mutex_lock(&gallery_mutex);
        visitors_completed++;
        pthread_mutex_unlock(&gallery_mutex);
    }
    
    return NULL;
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

// Функция очистки ресурсов при завершении
void cleanup() {
    if (output_file) {
        fclose(output_file);
        output_file = NULL;
    }
    
    if (paintings) {
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            sem_destroy(&paintings[i].semaphore);
        }
        free(paintings);
        paintings = NULL;
    }
    
    if (visitors) {
        for (int i = 0; i < TOTAL_VISITORS; i++) {
            if (visitors[i].visit_order) {
                free(visitors[i].visit_order);
                visitors[i].visit_order = NULL;
            }
        }
        free(visitors);
        visitors = NULL;
    }
    
    sem_destroy(&gallery_semaphore);
    
    print_message("Ресурсы освобождены. Программа завершена.");
}

int main(int argc, char* argv[]) {
    // Установка обработчика сигналов
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;
    sigaction(SIGINT, &sa, NULL);
    
    // Инициализация
    srand(time(NULL));
    
    // Проверка аргументов
    if (argc < 3) {
        printf("Использование:\n");
        printf("  %s <количество_посетителей> <выходной_файл>\n", argv[0]);
        printf("  %s -f <конфигурационный_файл> <выходной_файл>\n", argv[0]);
        printf("\nПримеры:\n");
        printf("  %s 200 output.txt\n", argv[0]);
        printf("  %s -f config.txt output.txt\n", argv[0]);
        return 1;
    }
    
    // Чтение конфигурации
    if (strcmp(argv[1], "-f") == 0) {
        if (argc < 4) {
            printf("Ошибка: недостаточно аргументов для опции -f\n");
            return 1;
        }
        
        FILE* config = fopen(argv[2], "r");
        if (!config) {
            printf("Ошибка открытия конфигурационного файла: %s\n", argv[2]);
            return 1;
        }
        
        if (fscanf(config, "%d", &TOTAL_VISITORS) != 1 ||
            fscanf(config, "%d", &MAX_IN_GALLERY) != 1 ||
            fscanf(config, "%d", &NUM_PAINTINGS) != 1 ||
            fscanf(config, "%d", &MAX_PER_PAINTING) != 1) {
            printf("Ошибка чтения конфигурационного файла\n");
            fclose(config);
            return 1;
        }
        
        fclose(config);
        output_file = fopen(argv[3], "w");
    } else {
        TOTAL_VISITORS = atoi(argv[1]);
        if (TOTAL_VISITORS < 100 || TOTAL_VISITORS > 300) {
            printf("Количество посетителей должно быть от 100 до 300\n");
            return 1;
        }
        output_file = fopen(argv[2], "w");
    }
    
    if (!output_file) {
        printf("Ошибка открытия выходного файла\n");
        return 1;
    }
    
    printf("\n=== КАРТИННАЯ ГАЛЕРЕЯ (Семафоры) ===\n");
    printf("Всего посетителей: %d\n", TOTAL_VISITORS);
    printf("Максимум в галерее: %d\n", MAX_IN_GALLERY);
    printf("Количество картин: %d\n", NUM_PAINTINGS);
    printf("Максимум у картины: %d\n", MAX_PER_PAINTING);
    printf("Для завершения нажмите Ctrl+C\n\n");
    
    // Инициализация семафоров
    if (sem_init(&gallery_semaphore, 0, MAX_IN_GALLERY) != 0) {
        perror("Ошибка инициализации семафора галереи");
        cleanup();
        return 1;
    }
    
    // Инициализация картин
    paintings = malloc(NUM_PAINTINGS * sizeof(Painting));
    for (int i = 0; i < NUM_PAINTINGS; i++) {
        paintings[i].id = i;
        paintings[i].current_visitors = 0;
        if (sem_init(&paintings[i].semaphore, 0, MAX_PER_PAINTING) != 0) {
            perror("Ошибка инициализации семафора картины");
            cleanup();
            return 1;
        }
    }
    
    // Создание посетителей
    visitors = malloc(TOTAL_VISITORS * sizeof(Visitor));
    for (int i = 0; i < TOTAL_VISITORS; i++) {
        visitors[i].id = i + 1;
        visitors[i].in_gallery = false;
        visitors[i].visit_order = generate_visit_order();
    }
    
    // Создание вахтера
    pthread_t guard;
    if (pthread_create(&guard, NULL, guard_thread, NULL) != 0) {
        perror("Ошибка создания потока вахтера");
        cleanup();
        return 1;
    }
    
    // Запуск посетителей
    for (int i = 0; i < TOTAL_VISITORS && running; i++) {
        if (pthread_create(&visitors[i].thread, NULL, visitor_thread, &visitors[i]) != 0) {
            perror("Ошибка создания потока посетителя");
            running = 0;
            break;
        }
        
        // Задержка между созданием посетителей
        usleep(100000 + rand() % 400000); // 0.1-0.5 секунды
        
        if (interrupt_requested) {
            print_message("Прерывание запрошено, останавливаем создание посетителей");
            break;
        }
    }
    
    // Ожидание завершения посетителей
    for (int i = 0; i < TOTAL_VISITORS; i++) {
        if (pthread_join(visitors[i].thread, NULL) != 0) {
            fprintf(stderr, "Ошибка ожидания потока посетителя %d\n", i+1);
        }
    }
    
    // Завершение вахтера
    running = 0;
    pthread_join(guard, NULL);
    
    // Итоговый отчет
    printf("\n=== ИТОГИ РАБОТЫ ГАЛЕРЕИ ===\n");
    printf("Всего посетителей запланировано: %d\n", TOTAL_VISITORS);
    printf("Посетителей вошло в галерею: %d\n", visitors_completed);
    printf("Прерывание запрошено: %s\n", interrupt_requested ? "ДА" : "НЕТ");
    
    if (output_file) {
        fprintf(output_file, "\n=== ИТОГИ РАБОТЫ ГАЛЕРЕИ ===\n");
        fprintf(output_file, "Всего посетителей запланировано: %d\n", TOTAL_VISITORS);
        fprintf(output_file, "Посетителей вошло в галерею: %d\n", visitors_completed);
        fprintf(output_file, "Прерывание запрошено: %s\n", interrupt_requested ? "ДА" : "НЕТ");
    }
    
    // Очистка ресурсов
    cleanup();
    
    return 0;
}