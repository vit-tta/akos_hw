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
#include <map>
#include <atomic>

// Флаги завершения
std::atomic<bool> stop_gallery(false);
std::atomic<bool> stop_observer(false);

// Константы
const int MAX_VISITORS = 25;
const int TOTAL_PAINTINGS = 5;
const int MAX_AT_PAINTING = 5;
const int TOTAL_VISITORS = 30;
const int OBSERVER_BUFFER_SIZE = 50; // Буфер для сообщений наблюдателя

// Обработчики сигналов
void gallery_signal_handler(int signum) {
    std::cout << "\nГалерея: Получен сигнал " << signum << ". Завершаем работу..." << std::endl;
    stop_gallery = true;
}

void observer_signal_handler(int signum) {
    std::cout << "\nНаблюдатель: Получен сигнал " << signum << ". Завершаем работу..." << std::endl;
    stop_observer = true;
}

// Структура сообщения для наблюдателя
struct ObserverMessage {
    int process_id;
    char message[256];
    time_t timestamp;
    int message_type; // 0: вахтер, 1: посетитель
    int current_visitors;
    int queue_size;
    int painting_visitors[5];
    int total_served;
};

// Структура для разделяемой памяти с наблюдателем
struct ObserverData {
    ObserverMessage messages[OBSERVER_BUFFER_SIZE];
    int write_index;
    int read_index;
    int message_count;
    std::atomic<bool> observer_connected;
};

// Основная структура для разделяемой памяти
struct SharedData {
    int current_visitors;
    int visitors_served;
    int visitors_at_painting[5];
    std::atomic<bool> termination_requested;
    int visitor_states[30]; // -1: не пришел, 0: в галерее, 1-4: у картины, 5: завершил
    int waiting_queue[30];
    int queue_size;
    
    // Данные для наблюдателя
    ObserverData observer_data;
};

class GalleryWithObserver {
private:
    SharedData* shared_data;
    int process_id;
    bool is_guard;
    std::mt19937 rng;
    sem_t* mutex;
    sem_t* painting_sem[5];
    sem_t* guard_sem;
    sem_t* visitor_sem[30];
    sem_t* observer_mutex;
    sem_t* observer_sem;
    int shm_fd;
    
public:
    GalleryWithObserver(int id, bool guard = false) : process_id(id), is_guard(guard) {
        rng.seed(time(nullptr) + id + (guard ? 1000 : 0));
        
        // Создаем/открываем разделяемую память
        shm_fd = shm_open("/gallery_shm_simple", O_CREAT | O_RDWR, 0666);
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
        mutex = sem_open("/gallery_mutex_simple", O_CREAT, 0666, 1);
        if (mutex == SEM_FAILED) {
            perror("sem_open mutex failed");
            exit(1);
        }
        
        guard_sem = sem_open("/guard_sem_simple", O_CREAT, 0666, 0);
        if (guard_sem == SEM_FAILED) {
            perror("sem_open guard_sem failed");
            exit(1);
        }
        
        // Семафоры для наблюдателя
        observer_mutex = sem_open("/observer_mutex_simple", O_CREAT, 0666, 1);
        if (observer_mutex == SEM_FAILED) {
            perror("sem_open observer_mutex failed");
            exit(1);
        }
        
        observer_sem = sem_open("/observer_sem_simple", O_CREAT, 0666, 0);
        if (observer_sem == SEM_FAILED) {
            perror("sem_open observer_sem failed");
            exit(1);
        }
        
        // Семафоры для картин
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_simple_" + std::to_string(i);
            painting_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, MAX_AT_PAINTING);
            if (painting_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        // Семафоры для посетителей
        for (int i = 0; i < 30; i++) {
            std::string sem_name = "/visitor_sem_simple_" + std::to_string(i);
            visitor_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, 0);
            if (visitor_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        if (is_guard) {
            std::cout << "👮 Вахтер запущен (PID: " << getpid() << ")" << std::endl;
        } else {
            std::cout << "👤 Посетитель " << process_id << " запущен (PID: " << getpid() << ")" << std::endl;
        }
    }
    
    ~GalleryWithObserver() {
        // Закрываем ресурсы
        sem_close(mutex);
        sem_close(guard_sem);
        sem_close(observer_mutex);
        sem_close(observer_sem);
        for (int i = 0; i < 5; i++) {
            sem_close(painting_sem[i]);
        }
        for (int i = 0; i < 30; i++) {
            sem_close(visitor_sem[i]);
        }
        munmap(shared_data, sizeof(SharedData));
        close(shm_fd);
    }
    
    void send_message(const std::string& msg) {
        // Вывод в консоль процесса
        if (is_guard) {
            std::cout << "👮 " << msg << std::endl;
        } else {
            std::cout << "👤 Посетитель " << process_id << ": " << msg << std::endl;
        }
        
        // Отправка сообщения наблюдателю через разделяемую память
        sem_wait(observer_mutex);
        
        if (shared_data->observer_data.observer_connected) {
            ObserverMessage message;
            message.process_id = process_id;
            message.message_type = is_guard ? 0 : 1;
            strncpy(message.message, msg.c_str(), sizeof(message.message) - 1);
            message.message[sizeof(message.message) - 1] = '\0';
            message.timestamp = time(nullptr);
            
            // Копируем текущее состояние
            message.current_visitors = shared_data->current_visitors;
            message.total_served = shared_data->visitors_served;
            message.queue_size = shared_data->queue_size;
            for (int i = 0; i < 5; i++) {
                message.painting_visitors[i] = shared_data->visitors_at_painting[i];
            }
            
            // Добавляем сообщение в буфер
            int write_index = shared_data->observer_data.write_index;
            shared_data->observer_data.messages[write_index] = message;
            shared_data->observer_data.write_index = (write_index + 1) % OBSERVER_BUFFER_SIZE;
            shared_data->observer_data.message_count++;
            
            // Уведомляем наблюдателя
            sem_post(observer_sem);
        }
        
        sem_post(observer_mutex);
    }
    
    void initialize_shared_data() {
        sem_wait(mutex);
        sem_wait(observer_mutex);
        
        std::cout << "🎯 Инициализация галереи..." << std::endl;
        shared_data->current_visitors = 0;
        shared_data->visitors_served = 0;
        shared_data->termination_requested = false;
        shared_data->queue_size = 0;
        
        // Инициализация данных наблюдателя
        shared_data->observer_data.write_index = 0;
        shared_data->observer_data.read_index = 0;
        shared_data->observer_data.message_count = 0;
        shared_data->observer_data.observer_connected = false;
        
        for (int i = 0; i < 5; i++) {
            shared_data->visitors_at_painting[i] = 0;
        }
        for (int i = 0; i < 30; i++) {
            shared_data->visitor_states[i] = -1;
            shared_data->waiting_queue[i] = -1;
        }
        
        sem_post(observer_mutex);
        sem_post(mutex);
    }
    
    void cleanup_resources() {
        std::cout << "🧹 Очистка ресурсов..." << std::endl;
        
        sem_unlink("/gallery_mutex_simple");
        sem_unlink("/guard_sem_simple");
        sem_unlink("/observer_mutex_simple");
        sem_unlink("/observer_sem_simple");
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_simple_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        for (int i = 0; i < 30; i++) {
            std::string sem_name = "/visitor_sem_simple_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        
        shm_unlink("/gallery_shm_simple");
    }
    
    void run_guard() {
        send_message("начал работу");
        
        // Инициализация если первый вахтер
        if (process_id == 0) {
            sleep(1);
            initialize_shared_data();
        } else {
            sleep(2);
        }
        
        int cycle = 0;
        while (!stop_gallery && !shared_data->termination_requested && 
               shared_data->visitors_served < TOTAL_VISITORS) {
            
            sem_wait(mutex);
            
            // Добавляем новых посетителей в очередь
            for (int i = 0; i < TOTAL_VISITORS; i++) {
                if (shared_data->visitor_states[i] == -1 && (rng() % 8 == 0)) {
                    shared_data->visitor_states[i] = -2; // В очереди
                    if (shared_data->queue_size < 30) {
                        shared_data->waiting_queue[shared_data->queue_size] = i;
                        shared_data->queue_size++;
                        send_message("посетитель " + std::to_string(i) + " встал в очередь");
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
            if (cycle % 10 == 0) {
                send_message("статистика: в галерее " + std::to_string(shared_data->current_visitors) +
                           ", в очереди " + std::to_string(shared_data->queue_size) +
                           ", обслужено " + std::to_string(shared_data->visitors_served));
            }
            
            sem_post(mutex);
            cycle++;
            usleep(300000); // 0.3 секунды
        }
        
        send_message("завершил работу");
        shared_data->termination_requested = true;
        
        // Разбудить всех ожидающих
        for (int i = 0; i < TOTAL_VISITORS; i++) {
            sem_post(visitor_sem[i]);
        }
        
        if (process_id == 0) {
            cleanup_resources();
        }
    }
    
    void run_visitor() {
        // Случайная задержка перед приходом
        int delay = 300 + (rng() % 1200);
        usleep(delay * 1000);
        
        if (stop_gallery || shared_data->termination_requested) return;
        
        send_message("пришел в галерею");
        
        // Ожидаем разрешения войти
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 20; // Таймаут 20 секунд
        
        if (sem_timedwait(visitor_sem[process_id], &ts) != 0) {
            send_message("не дождался входа (таймаут)");
            return;
        }
        
        if (stop_gallery || shared_data->termination_requested) return;
        
        send_message("вошел в галерею");
        
        std::vector<bool> viewed_paintings(5, false);
        int paintings_viewed = 0;
        
        while (!stop_gallery && !shared_data->termination_requested && paintings_viewed < 5) {
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
            
            // Пытаемся подойти к картине
            send_message("хочет посмотреть картину " + std::to_string(painting));
            
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5;
            
            if (sem_timedwait(painting_sem[painting], &ts) == 0) {
                // Успешно подошли к картине
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]++;
                shared_data->visitor_states[process_id] = painting + 1;
                send_message("смотрит картину " + std::to_string(painting) + 
                           " (зрителей: " + std::to_string(shared_data->visitors_at_painting[painting]) + ")");
                sem_post(mutex);
                
                // Время осмотра
                int view_time = 500 + (rng() % 1500);
                usleep(view_time * 1000);
                
                if (stop_gallery || shared_data->termination_requested) {
                    sem_post(painting_sem[painting]);
                    return;
                }
                
                // Завершаем осмотр
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]--;
                viewed_paintings[painting] = true;
                paintings_viewed++;
                shared_data->visitor_states[process_id] = 0;
                send_message("закончил смотреть картину " + std::to_string(painting) + 
                           " (осмотрено: " + std::to_string(paintings_viewed) + "/5)");
                sem_post(mutex);
                
                sem_post(painting_sem[painting]);
                
                // Пауза между картинами
                usleep(200000);
            } else {
                send_message("не может подойти к картине " + std::to_string(painting) + " (много народу)");
                usleep(400000);
            }
        }
        
        // Выход из галереи
        sem_wait(mutex);
        shared_data->current_visitors--;
        shared_data->visitors_served++;
        shared_data->visitor_states[process_id] = 5; // Завершил
        send_message("покинул галерею (осмотрено: " + std::to_string(paintings_viewed) + "/5 картин)");
        sem_post(mutex);
    }
    
    void run() {
        if (is_guard) {
            run_guard();
        } else {
            run_visitor();
        }
    }
};

class Observer {
private:
    SharedData* shared_data;
    int shm_fd;
    sem_t* observer_mutex;
    sem_t* observer_sem;
    
public:
    Observer() {
        // Открываем разделяемую память
        shm_fd = shm_open("/gallery_shm_simple", O_RDWR, 0666);
        if (shm_fd == -1) {
            perror("shm_open failed");
            exit(1);
        }
        
        shared_data = static_cast<SharedData*>(
            mmap(nullptr, sizeof(SharedData), PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0));
        
        if (shared_data == MAP_FAILED) {
            perror("mmap failed");
            exit(1);
        }
        
        // Открываем семафоры наблюдателя
        observer_mutex = sem_open("/observer_mutex_simple", 0);
        if (observer_mutex == SEM_FAILED) {
            perror("sem_open observer_mutex failed");
            exit(1);
        }
        
        observer_sem = sem_open("/observer_sem_simple", 0);
        if (observer_sem == SEM_FAILED) {
            perror("sem_open observer_sem failed");
            exit(1);
        }
        
        // Регистрируем наблюдателя
        sem_wait(observer_mutex);
        shared_data->observer_data.observer_connected = true;
        shared_data->observer_data.write_index = 0;
        shared_data->observer_data.read_index = 0;
        shared_data->observer_data.message_count = 0;
        sem_post(observer_mutex);
        
        std::cout << "=== НАБЛЮДАТЕЛЬ ГАЛЕРЕИ ===" << std::endl;
        std::cout << "✅ Наблюдатель подключен к системе" << std::endl;
        std::cout << "📊 Размер буфера: " << OBSERVER_BUFFER_SIZE << " сообщений" << std::endl;
        std::cout << "🕐 Ожидание сообщений..." << std::endl;
        std::cout << "============================" << std::endl;
    }
    
    ~Observer() {
        // Отключаем наблюдателя
        sem_wait(observer_mutex);
        shared_data->observer_data.observer_connected = false;
        sem_post(observer_mutex);
        
        sem_close(observer_mutex);
        sem_close(observer_sem);
        munmap(shared_data, sizeof(SharedData));
        close(shm_fd);
    }
    
    void run() {
        int message_count = 0;
        time_t last_report = time(nullptr);
        
        while (!stop_observer) {
            struct timespec ts;
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 1; // Таймаут 1 секунда
            
            // Ожидаем новые сообщения
            if (sem_timedwait(observer_sem, &ts) == 0) {
                sem_wait(observer_mutex);
                
                // Читаем все доступные сообщения
                while (shared_data->observer_data.message_count > 0) {
                    int read_index = shared_data->observer_data.read_index;
                    ObserverMessage msg = shared_data->observer_data.messages[read_index];
                    
                    // Форматируем время
                    char time_buf[64];
                    struct tm* tm_info = localtime(&msg.timestamp);
                    strftime(time_buf, sizeof(time_buf), "%H:%M:%S", tm_info);
                    
                    // Выводим сообщение
                    std::string type_str = (msg.message_type == 0) ? "👮 ВАХТЕР" : "👤 ПОСЕТИТЕЛЬ";
                    std::cout << "[" << time_buf << "] " << type_str << " " << msg.process_id 
                              << ": " << msg.message 
                              << " [В галерее: " << msg.current_visitors << "/" << MAX_VISITORS
                              << ", Очередь: " << msg.queue_size 
                              << ", Обслужено: " << msg.total_served << "]"
                              << std::endl;
                    
                    // Обновляем индекс
                    shared_data->observer_data.read_index = (read_index + 1) % OBSERVER_BUFFER_SIZE;
                    shared_data->observer_data.message_count--;
                    message_count++;
                }
                
                sem_post(observer_mutex);
                
                // Периодический отчет
                time_t now = time(nullptr);
                if (now - last_report >= 8) {
                    std::cout << "--- 📈 СТАТИСТИКА (" << message_count << " сообщений) ---" << std::endl;
                    last_report = now;
                }
            }
        }
        
        std::cout << "============================" << std::endl;
        std::cout << "👋 Наблюдатель завершил работу" << std::endl;
        std::cout << "📨 Всего сообщений: " << message_count << std::endl;
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Использование:" << std::endl;
        std::cout << "  " << argv[0] << " observer    - запуск наблюдателя" << std::endl;
        std::cout << "  " << argv[0] << " guard       - запуск вахтера" << std::endl;
        std::cout << "  " << argv[0] << " visitor <id>- запуск посетителя (id: 0-29)" << std::endl;
        std::cout << std::endl;
        std::cout << "Порядок запуска:" << std::endl;
        std::cout << "  1. Сначала вахтер или посетители" << std::endl;
        std::cout << "  2. Потом наблюдатель" << std::endl;
        return 1;
    }
    
    if (strcmp(argv[1], "observer") == 0) {
        std::signal(SIGINT, observer_signal_handler);
        std::signal(SIGTERM, observer_signal_handler);
        Observer observer;
        observer.run();
    }
    else if (strcmp(argv[1], "guard") == 0) {
        std::signal(SIGINT, gallery_signal_handler);
        std::signal(SIGTERM, gallery_signal_handler);
        GalleryWithObserver guard(0, true);
        guard.run();
    }
    else if (strcmp(argv[1], "visitor") == 0) {
        if (argc != 3) {
            std::cout << "Укажите ID посетителя: " << argv[0] << " visitor <id>" << std::endl;
            return 1;
        }
        int id = std::atoi(argv[2]);
        if (id < 0 || id >= TOTAL_VISITORS) {
            std::cout << "ID должен быть от 0 до " << TOTAL_VISITORS - 1 << std::endl;
            return 1;
        }
        std::signal(SIGINT, gallery_signal_handler);
        std::signal(SIGTERM, gallery_signal_handler);
        GalleryWithObserver visitor(id, false);
        visitor.run();
    }
    else {
        std::cout << "Неизвестная команда: " << argv[1] << std::endl;
        return 1;
    }
    
    return 0;
}