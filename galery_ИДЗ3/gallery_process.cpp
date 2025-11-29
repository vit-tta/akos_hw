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
#include <queue>
#include <atomic>

volatile sig_atomic_t stop_flag = 0;
const int MAX_VISITORS = 25;
const int TOTAL_PAINTINGS = 5;
const int MAX_AT_PAINTING = 5;
const int TOTAL_VISITORS = 150;

void signal_handler(int signum) {
    stop_flag = 1;
}

struct SharedData {
    int current_visitors;
    int visitors_served;
    int visitors_at_painting[5];
    bool termination_requested;
    int visitor_positions[150]; // -1: не пришел, -2: в очереди, -3: завершил, 0-4: у картины
};

class GalleryProcess {
private:
    SharedData* shared_data;
    int process_id;
    std::mt19937 rng;
    sem_t* mutex;
    sem_t* painting_sem[5];
    sem_t* guard_sem;
    sem_t* visitor_sem[150];
    int shm_fd;
    bool is_guard;
    
public:
    GalleryProcess(int id, bool guard = false) : process_id(id), is_guard(guard) {
        rng.seed(time(nullptr) + id + (guard ? 1000 : 0));
        
        shm_fd = shm_open("/gallery_shm", O_CREAT | O_RDWR, 0666);
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
        
        mutex = sem_open("/gallery_mutex", O_CREAT, 0666, 1);
        if (mutex == SEM_FAILED) {
            perror("sem_open mutex failed");
            exit(1);
        }
        
        guard_sem = sem_open("/guard_sem", O_CREAT, 0666, 0);
        if (guard_sem == SEM_FAILED) {
            perror("sem_open guard_sem failed");
            exit(1);
        }
        
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_" + std::to_string(i);
            painting_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, MAX_AT_PAINTING);
            if (painting_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        for (int i = 0; i < 150; i++) {
            std::string sem_name = "/visitor_sem_" + std::to_string(i);
            visitor_sem[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, 0);
            if (visitor_sem[i] == SEM_FAILED) {
                perror(("sem_open " + sem_name).c_str());
                exit(1);
            }
        }
        
        if (is_guard) {
            std::cout << "👮 Вахтер процесса запущен (PID: " << getpid() << ")" << std::endl;
        } else {
            std::cout << "👤 Посетитель " << process_id << " процесса запущен (PID: " << getpid() << ")" << std::endl;
        }
    }
    
    ~GalleryProcess() {
        sem_close(mutex);
        sem_close(guard_sem);
        for (int i = 0; i < 5; i++) {
            sem_close(painting_sem[i]);
        }
        for (int i = 0; i < 150; i++) {
            sem_close(visitor_sem[i]);
        }
        munmap(shared_data, sizeof(SharedData));
        close(shm_fd);
    }
    
    void initialize_shared_data() {
        sem_wait(mutex);
        shared_data->current_visitors = 0;
        shared_data->visitors_served = 0;
        shared_data->termination_requested = false;
        for (int i = 0; i < 5; i++) {
            shared_data->visitors_at_painting[i] = 0;
        }
        for (int i = 0; i < 150; i++) {
            shared_data->visitor_positions[i] = -1; // Не пришли
        }
        sem_post(mutex);
    }
    
    void cleanup_resources() {
        std::cout << "Очистка ресурсов галереи..." << std::endl;
        
        sem_unlink("/gallery_mutex");
        sem_unlink("/guard_sem");
        for (int i = 0; i < 5; i++) {
            std::string sem_name = "/painting_sem_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        for (int i = 0; i < 150; i++) {
            std::string sem_name = "/visitor_sem_" + std::to_string(i);
            sem_unlink(sem_name.c_str());
        }
        
        shm_unlink("/gallery_shm");
    }
    
    void run_guard() {
        std::cout << "👮 Вахтер начал контролировать галерею" << std::endl;
        
        // Ожидание посетителей
        while (!stop_flag && shared_data->visitors_served < TOTAL_VISITORS) {
            // Проверка новых посетителей
            sem_wait(mutex);
            for (int i = 0; i < TOTAL_VISITORS; i++) {
                if (shared_data->visitor_positions[i] == -1) {
                    // Новый посетитель пришел
                    shared_data->visitor_positions[i] = -2; // В очереди
                    std::cout << "👤 Посетитель " << i << " пришел в галерею" << std::endl;
                }
            }
            
            // Пропуск посетителей из очереди
            for (int i = 0; i < TOTAL_VISITORS; i++) {
                if (shared_data->visitor_positions[i] == -2 && shared_data->current_visitors < MAX_VISITORS) {
                    shared_data->visitor_positions[i] = 0; // В галерее
                    shared_data->current_visitors++;
                    sem_post(visitor_sem[i]); // Разрешаем войти
                    std::cout << "✅ Вахтер пропустил посетителя " << i 
                              << " (в галерее: " << shared_data->current_visitors << ")" << std::endl;
                }
            }
            sem_post(mutex);
            
            usleep(500000); // Проверка каждые 0.5 секунды
        }
        
        std::cout << "👮 Вахтер завершил работу" << std::endl;
    }
    
    void run_visitor() {
        // Случайная задержка прибытия
        int arrival_delay = 500 + (rng() % 2000);
        usleep(arrival_delay * 1000);
        
        if (stop_flag) return;
        
        // Ожидание разрешения войти
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += 30; // Таймаут 30 секунд
        
        int result = sem_timedwait(visitor_sem[process_id], &ts);
        if (result != 0) {
            std::cout << "👤 Посетитель " << process_id << " не дождался входа" << std::endl;
            return;
        }
        
        if (stop_flag) return;
        
        std::cout << "👤 Посетитель " << process_id << " вошел в галерею" << std::endl;
        
        bool viewed_paintings[5] = {false};
        int paintings_viewed = 0;
        
        while (!stop_flag && paintings_viewed < 5) {
            // Выбор случайной непросмотренной картины
            int painting = -1;
            for (int i = 0; i < 5; i++) {
                if (!viewed_paintings[i] && (rng() % 5 == 0 || painting == -1)) {
                    painting = i;
                }
            }
            
            if (painting == -1) break;
            
            // Попытка подойти к картине
            clock_gettime(CLOCK_REALTIME, &ts);
            ts.tv_sec += 5; // Таймаут 5 секунд
            
            if (sem_timedwait(painting_sem[painting], &ts) == 0) {
                // Успешно подошли к картине
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]++;
                shared_data->visitor_positions[process_id] = painting;
                std::cout << "🖼️ Посетитель " << process_id << " подошел к картине " << painting 
                          << " (у картины: " << shared_data->visitors_at_painting[painting] << ")" << std::endl;
                sem_post(mutex);
                
                // Осмотр картины
                int view_time = 1000 + (rng() % 2000);
                usleep(view_time * 1000);
                
                if (stop_flag) {
                    sem_post(painting_sem[painting]);
                    return;
                }
                
                sem_wait(mutex);
                shared_data->visitors_at_painting[painting]--;
                viewed_paintings[painting] = true;
                paintings_viewed++;
                std::cout << "✅ Посетитель " << process_id << " осмотрел картину " << painting 
                          << " (осмотрено: " << paintings_viewed << "/5)" << std::endl;
                sem_post(mutex);
                
                sem_post(painting_sem[painting]);
                
                // Пауза между картинами
                usleep(500000);
            }
        }
        
        // Выход из галереи
        sem_wait(mutex);
        shared_data->current_visitors--;
        shared_data->visitors_served++;
        shared_data->visitor_positions[process_id] = -3; // Завершил
        std::cout << "🚪 Посетитель " << process_id << " покинул галерею" << std::endl;
        sem_post(mutex);
    }
    
    void run() {
        if (is_guard) {
            if (process_id == 0) {
                initialize_shared_data();
            }
            run_guard();
        } else {
            run_visitor();
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc < 2) {
        std::cout << "Использование:" << std::endl;
        std::cout << "  Для вахтера: " << argv[0] << " guard" << std::endl;
        std::cout << "  Для посетителя: " << argv[0] << " visitor <id_посетителя(0-149)>" << std::endl;
        return 1;
    }
    
    std::signal(SIGINT, signal_handler);
    std::signal(SIGTERM, signal_handler);
    
    if (strcmp(argv[1], "guard") == 0) {
        std::atexit([]() {
            GalleryProcess temp(0, true);
            temp.cleanup_resources();
        });
        
        GalleryProcess guard(0, true);
        guard.run();
    } else if (strcmp(argv[1], "visitor") == 0) {
        if (argc != 3) {
            std::cout << "Для посетителя укажите ID: " << argv[0] << " visitor <id>" << std::endl;
            return 1;
        }
        
        int visitor_id = std::atoi(argv[2]);
        if (visitor_id < 0 || visitor_id >= TOTAL_VISITORS) {
            std::cout << "ID посетителя должен быть от 0 до " << TOTAL_VISITORS - 1 << std::endl;
            return 1;
        }
        
        GalleryProcess visitor(visitor_id, false);
        visitor.run();
    } else {
        std::cout << "Неизвестный режим: " << argv[1] << std::endl;
        return 1;
    }
    
    return 0;
}