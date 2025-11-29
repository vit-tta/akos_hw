#include <iostream>
#include <unistd.h>
#include <sys/mman.h>
#include <semaphore.h>
#include <fcntl.h>
#include <cstdlib>
#include <ctime>
#include <csignal>
#include <cstring>
#include <random>
#include <vector>
#include <atomic>

// Флаги завершения
std::atomic<bool> stop_flag(false);

// Константы
const int MAX_VISITORS = 25;
const int TOTAL_PAINTINGS = 5;
const int MAX_AT_PAINTING = 5;
const int TOTAL_VISITORS = 30;
const int MAX_OBSERVERS = 3;

// Обработчик сигналов
void signal_handler(int signum) {
    std::cout << "\nПолучен сигнал " << signum << ". Завершаем работу..." << std::endl;
    stop_flag = true;
}

// Структура для наблюдателя
struct ObserverInfo {
    pid_t pid;
    bool active;
    char padding[64]; // Выравнивание для избежания false sharing
};

// Основная структура для разделяемой памяти
struct SharedData {
    // Основные данные галереи
    int current_visitors;
    int visitors_served;
    int visitors_at_painting[5];
    bool termination_requested;
    
    // Данные посетителей
    int visitor_states[30]; // -1: не пришел, 0: в галерее, 1-4: у картины, 5: завершил
    int waiting_queue[30];
    int queue_size;
    
    // Данные наблюдателей
    ObserverInfo observers[MAX_OBSERVERS];
    
    // Буфер сообщений (простой циклический буфер)
    struct Message {
        int process_id;
        int message_type; // 0: вахтер, 1: посетитель
        char text[100];
        int data_current_visitors;
        int data_queue_size;
        int data_served;
    } messages[100];
    
    int msg_write_index;
    int msg_read_index;
    int msg_count;
};

class GallerySystem {
private:
    SharedData* shared_data;
    int process_id;
    bool is_guard;
    bool is_observer;
    int observer_id;
    std::mt19937 rng;
    
    // Семафоры
    sem_t* mutex;
    sem_t* painting_sem[5];
    sem_t* visitor_sem[30];
    sem_t* msg_mutex;
    sem_t* msg_sem;
    
    int shm_fd;

public:
    GallerySystem(int id, bool guard = false, bool observer = false, int obs_id = -1) 
        : process_id(id), is_guard(guard), is_observer(observer), observer_id(obs_id) {
        
        rng.seed(time(nullptr) + id + (guard ? 1000 : 0) + (observer ? 2000 : 0));
        
        // Создаем/открываем разделяемую память
        shm_fd = shm_open("/gallery_sys", O_CREAT | O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open failed");
            exit(1);
        }
        
        if (ftruncate(shm_fd, sizeof(SharedData)) == -1) {
            perror("ftruncate failed");
            exit(1);
        }
        
        shared_data = static_cast<SharedData*>(
            mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
        
        if (shared_data == MAP_FAILED) {
            perror("mmap failed");
            exit(1);
        }
        
        // Создаем/открываем семафоры
        mutex = sem_open("/gallery_mutex", O_CREAT, 0666, 1);
        if (mutex == SEM_FAILED) {
            perror("sem_open mutex failed");
            exit(1);
        }
        
        msg_mutex = sem_open("/msg_mutex", O_CREAT, 0666, 1);
        if (msg_mutex == SEM_FAILED) {
            perror("sem_open msg_mutex failed");
            exit(1);
        }
        
        msg_sem = sem_open("/msg_sem", O_CREAT, 0666, 0);
        if (msg_sem == SEM_FAILED) {
            perror("sem_open msg_sem failed");
            exit(1);
        }
        
        // Семафоры для картин
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_" + std::to_string(i);
            painting_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, MAX_AT_PAINTING);
            if (painting_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        // Семафоры для посетителей
        for (int i = 0; i < 30; i++) {
            std::string sem_name = "/visitor_sem_" + std::to_string(i);
            visitor_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, 0);
            if (visitor_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        if (is_guard) {
            std::cout << "👮 Вахтер запущен (PID: " << getpid() << ")" << std::endl;
        } else if (is_observer) {
            std::cout << "👀 Наблюдатель " << observer_id << " запущен (PID: " << getpid() << ")" << std::endl;
        } else {
            std::cout << "👤 Посетитель " << process_id << " запущен (PID: " << getpid() << ")" << std::endl;
        }
    }
    
    ~GallerySystem() {
        // Закрываем ресурсы
        sem_close(mutex);
        sem_close(msg_mutex);
        sem_close(msg_sem);
        for (int i = 0; i < 5; i++) {
            sem_close(painting_sem[i]);
        }
        for (int i = 0; i < 30; i++) {
            sem_close(visitor_sem[i]);
        }
        munmap(shared_data, sizeof(SharedData));
        close(shm_fd);
    }
    
    void initialize_shared_data() {
        sem_wait(mutex);
        sem_wait(msg_mutex);
        
        std::cout << "🎯 Инициализация системы галереи..." << std::endl;
        
        // Основные данные
        shared_data->current_visitors = 0;
        shared_data->visitors_served = 0;
        shared_data->termination_requested = false;
        shared_data->queue_size = 0;
        
        // Данные наблюдателей
        for (int i = 0; i < MAX_OBSERVERS; i++) {
            shared_data->observers[i].pid = 0;
            shared_data->observers[i].active = false;
        }
        
        // Буфер сообщений
        shared_data->msg_write_index = 0;
        shared_data->msg_read_index = 0;
        shared_data->msg_count = 0;
        
        // Данные посетителей
        for (int i = 0; i < 5; i++) {
            shared_data->visitors_at_painting[i] = 0;
        }
        for (int i = 0; i < 30; i++) {
            shared_data->visitor_states[i] = -1;
            shared_data->waiting_queue[i] = -1;
        }
        
        sem_post(msg_mutex);
        sem_post(mutex);
    }
    
    void send_message(const std::string& msg) {
        // Вывод в консоль процесса
        if (is_guard) {
            std::cout << "👮 " << msg << std::endl;
        } else if (is_observer) {
            std::cout << "👀 Наблюдатель " << observer_id << ": " << msg << std::endl;
        } else {
            std::cout << "👤 Посетитель " << process_id << ": " << msg << std::endl;
        }
        
        // Добавление сообщения в буфер
        sem_wait(msg_mutex);
        
        SharedData::Message message;
        message.process_id = process_id;
        message.message_type = is_guard ? 0 : (is_observer ? 2 : 1);
        strncpy(message.text, msg.c_str(), sizeof(message.text) - 1);
        message.text[sizeof(message.text) - 1] = '\0';
        
        // Копируем текущее состояние
        message.data_current_visitors = shared_data->current_visitors;
        message.data_queue_size = shared_data->queue_size;
        message.data_served = shared_data->visitors_served;
        
        // Добавляем в буфер
        shared_data->messages[shared_data->msg_write_index] = message;
        shared_data->msg_write_index = (shared_data->msg_write_index + 1) % 100;
        shared_data->msg_count++;
        
        // Уведомляем наблюдателей
        sem_post(msg_sem);
        
        sem_post(msg_mutex);
    }
    
    void register_observer() {
        sem_wait(mutex);
        
        // Регистрируем наблюдателя
        if (observer_id >= 0 && observer_id < MAX_OBSERVERS) {
            shared_data->observers[observer_id].pid = getpid();
            shared_data->observers[observer_id].active = true;
            send_message("зарегистрирован в системе");
        }
        
        sem_post(mutex);
    }
    
    void unregister_observer() {
        sem_wait(mutex);
        
        // Отключаем наблюдателя
        if (observer_id >= 0 && observer_id < MAX_OBSERVERS) {
            shared_data->observers[observer_id].active = false;
        }
        
        sem_post(mutex);
    }
    
    void cleanup_resources() {
        std::cout << "🧹 Очистка ресурсов..." << std::endl;
        
        sem_unlink("/gallery_mutex");
        sem_unlink("/msg_mutex");
        sem_unlink("/msg_sem");
        
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        for (int i = 0; i < 30; i++) {
            std::string sem_name = "/visitor_sem_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        
        shm_unlink("/gallery_sys");
    }
    
    void run_guard() {
        send_message("начал работу");
        
        // Инициализация если первый процесс
        if (process_id == 0) {
            sleep(1);
            initialize_shared_data();
        } else {
            sleep(2);
        }
        
        int cycle = 0;
        
        while (!stop_flag && !shared_data->termination_requested && 
               shared_data->visitors_served < TOTAL_VISITORS) {
            
            sem_wait(mutex);
            
            // Создаем новых посетителей
            for (int i = 0; i < TOTAL_VISITORS; i++) {
                if (shared_data->visitor_states[i] == -1 && (rng() % 10 == 0)) {
                    shared_data->visitor_states[i] = -2; // В очереди
                    if (shared_data->queue_size < 30) {
                        shared_data->waiting_queue[shared_data->queue_size] = i;
                        shared_data->queue_size++;
                        send_message("посетитель " + std::to_string(i) + " пришел в галерею");
                    }
                }
            }
            
            // Пропускаем посетителей из очереди
            if (shared_data->queue_size > 0 && shared_data->current_visitors < MAX_VISITORS) {
                int visitor_id = shared_data->waiting_queue[0];
                
                // Сдвигаем очередь
                for (int i = 0; i < shared_data->queue_size - 1; i++) {
                    shared_data->waiting_queue[i] = shared_data->waiting_queue[i + 1];
                }
                shared_data->queue_size--;
                
                shared_data->visitor_states[visitor_id] = 0; // В галерее
                shared_data->current_visitors++;
                
                sem_post(visitor_sem[visitor_id]); // Разрешаем войти
                send_message("пропустил посетителя " + std::to_string(visitor_id) + 
                           " (в галерее: " + std::to_string(shared_data->current_visitors) + ")");
            }
            
            // Статистика
            if (cycle % 15 == 0) {
                int active_observers = 0;
                for (int i = 0; i < MAX_OBSERVERS; i++) {
                    if (shared_data->observers[i].active) active_observers++;
                }
                
                send_message("статистика: в галерее " + std::to_string(shared_data->current_visitors) +
                           ", в очереди " + std::to_string(shared_data->queue_size) +
                           ", обслужено " + std::to_string(shared_data->visitors_served) +
                           ", наблюдателей: " + std::to_string(active_observers));
            }
            
            sem_post(mutex);
            cycle++;
            usleep(500000); // 0.5 секунды
        }
        
        send_message("завершил работу");
        shared_data->termination_requested = true;
        
        // Будим всех ожидающих
        for (int i = 0; i < TOTAL_VISITORS; i++) {
            sem_post(visitor_sem[i]);
        }
        
        if (process_id == 0) {
            cleanup_resources();
        }
    }
    
    void run_visitor() {
        // Случайная задержка перед приходом
        int delay = 500 + (rng() % 1500);
        usleep(delay * 1000);
        
        if (stop_flag || shared_data->termination_requested) return;
        
        send_message("пришел в галерею");
        
        // Ожидаем разрешения войти
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 15;
        
        if (sem_timedwait(visitor_sem[process_id], &ts) != 0) {
            send_message("не дождался входа (таймаут)");
            return;
        }
        
        if (stop_flag || shared_data->termination_requested) return;
        
        send_message("вошел в галерею");
        
        std::vector<bool> viewed_paintings(5, false);
        int paintings_viewed = 0;
        
        while (!stop_flag && !shared_data->termination_requested && paintings_viewed < 5) {
            // Выбираем случайную непросмотренную картину
            int painting = -1;
            std::vector<int> available;
            for (int i = 0; i < 5; i++) {
                if (!viewed_paintings[i]) {
                    available.push_back(i);
                }
            }
            if (available.empty()) break;
            
            painting = available[rng() % available.size()];
            
            send_message("подходит к картине " + std::to_string(painting));
            
            // Пытаемся подойти к картине
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 3;
            
            if (sem_timedwait(painting_sem[painting], &ts) == 0) {
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]++;
                send_message("смотрит картину " + std::to_string(painting) + 
                           " (зрителей: " + std::to_string(shared_data->visitors_at_painting[painting]) + ")");
                sem_post(mutex);
                
                // Осмотр картины
                int view_time = 800 + (rng() % 1200);
                usleep(view_time * 1000);
                
                if (stop_flag || shared_data->termination_requested) {
                    sem_post(painting_sem[painting]);
                    return;
                }
                
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]--;
                viewed_paintings[painting] = true;
                paintings_viewed++;
                send_message("осмотрел картину " + std::to_string(painting) + 
                           " (осмотрено: " + std::to_string(paintings_viewed) + "/5)");
                sem_post(mutex);
                
                sem_post(painting_sem[painting]);
                usleep(300000); // Пауза
            } else {
                send_message("не может подойти к картине " + std::to_string(painting));
                usleep(500000);
            }
        }
        
        // Выход из галереи
        sem_wait(mutex);
        shared_data->current_visitors--;
        shared_data->visitors_served++;
        send_message("покинул галерею (осмотрено: " + std::to_string(paintings_viewed) + "/5 картин)");
        sem_post(mutex);
    }
    
    void run_observer() {
        send_message("начал наблюдение");
        register_observer();
        
        int message_count = 0;
        
        while (!stop_flag && !shared_data->termination_requested) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 2; // Таймаут 2 секунды
            
            // Ждем новые сообщения
            if (sem_timedwait(msg_sem, &ts) == 0) {
                sem_wait(msg_mutex);
                
                // Читаем все доступные сообщения
                while (shared_data->msg_count > 0) {
                    SharedData::Message msg = shared_data->messages[shared_data->msg_read_index];
                    
                    // Форматируем вывод
                    std::string type_str;
                    switch (msg.message_type) {
                        case 0: type_str = "👮 ВАХТЕР"; break;
                        case 1: type_str = "👤 ПОСЕТИТЕЛЬ"; break;
                        case 2: type_str = "👀 НАБЛЮДАТЕЛЬ"; break;
                        default: type_str = "❓ НЕИЗВЕСТНО";
                    }
                    
                    std::cout << "[" << observer_id << "] " << type_str << " " << msg.process_id 
                              << ": " << msg.text 
                              << " [В галерее: " << msg.data_current_visitors 
                              << ", Очередь: " << msg.data_queue_size 
                              << ", Обслужено: " << msg.data_served << "]"
                              << std::endl;
                    
                    shared_data->msg_read_index = (shared_data->msg_read_index + 1) % 100;
                    shared_data->msg_count--;
                    message_count++;
                }
                
                sem_post(msg_mutex);
                
                // Периодический отчет
                if (message_count % 10 == 0) {
                    std::cout << "--- Наблюдатель " << observer_id << ": " 
                              << message_count << " сообщений ---" << std::endl;
                }
            }
        }
        
        send_message("завершил наблюдение");
        unregister_observer();
    }
    
    void run() {
        if (is_guard) {
            run_guard();
        } else if (is_observer) {
            run_observer();
        } else {
            run_visitor();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Использование:" << std::endl;
        std::cout << "  " << argv[0] << " guard                    - запуск вахтера" << std::endl;
        std::cout << "  " << argv[0] << " visitor <id>             - запуск посетителя (0-29)" << std::endl;
        std::cout << "  " << argv[0] << " observer <id>            - запуск наблюдателя (0-2)" << std::endl;
        std::cout << std::endl;
        std::cout << "Пример:" << std::endl;
        std::cout << "  " << argv[0] << " guard" << std::endl;
        std::cout << "  " << argv[0] << " visitor 0" << std::endl;
        std::cout << "  " << argv[0] << " observer 0" << std::endl;
        std::cout << "  " << argv[0] << " observer 1" << std::endl;
        return 1;
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    if (strcmp(argv[1], "guard") == 0) {
        GallerySystem system(0, true);
        system.run();
    }
    else if (strcmp(argv[1], "visitor") == 0) {
        if (argc != 3) {
            std::cout << "Укажите ID посетителя: " << argv[0] << " visitor <id>" << std::endl;
            return 1;
        }
        int id = std::atoi(argv[2]);
        if (id < 0 || id >= TOTAL_VISITORS) {
            std::cout << "ID посетителя должен быть от 0 до " << TOTAL_VISITORS - 1 << std::endl;
            return 1;
        }
        GallerySystem system(id, false);
        system.run();
    }
    else if (strcmp(argv[1], "observer") == 0) {
        if (argc != 3) {
            std::cout << "Укажите ID наблюдателя: " << argv[0] << " observer <id>" << std::endl;
            return 1;
        }
        int id = std::atoi(argv[2]);
        if (id < 0 || id >= MAX_OBSERVERS) {
            std::cout << "ID наблюдателя должен быть от 0 до " << MAX_OBSERVERS - 1 << std::endl;
            return 1;
        }
        GallerySystem system(id, false, true, id);
        system.run();
    }
    else {
        std::cout << "Неизвестная команда: " << argv[1] << std::endl;
        return 1;
    }
    
    return 0;
}