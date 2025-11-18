#include "common.h"

class Visitor {
private:
    int visitor_id;
    GalleryState* gallery_state;
    MessageBuffer* message_buffer;
    sem_t* sem_gallery_entry;
    sem_t* sem_gallery_exit;
    sem_t* sem_paintings[NUM_PAINTINGS];
    sem_t* sem_watcher;
    sem_t* sem_message;
    std::mt19937 rng;
    
public:
    Visitor(int id) : visitor_id(id), rng(time(nullptr) + id) {
        // Подключаемся к разделяемой памяти
        int shm_fd = shm_open(SHM_GALLERY_STATE, O_RDWR, 0666);
        gallery_state = (GalleryState*)mmap(NULL, sizeof(GalleryState), 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        
        int msg_fd = shm_open(SHM_MESSAGES, O_RDWR, 0666);
        message_buffer = (MessageBuffer*)mmap(NULL, sizeof(MessageBuffer), 
                                            PROT_READ | PROT_WRITE, MAP_SHARED, msg_fd, 0);
        
        // Открываем семафоры
        sem_gallery_entry = sem_open(SEM_GALLERY_ENTRY, 0);
        sem_gallery_exit = sem_open(SEM_GALLERY_EXIT, 0);
        sem_watcher = sem_open(SEM_WATCHER, 0);
        sem_message = sem_open(SEM_MESSAGE, 0);
        
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            std::string sem_name = std::string(SEM_PAINTING_BASE) + std::to_string(i);
            sem_paintings[i] = sem_open(sem_name.c_str(), O_CREAT, 0666, MAX_PER_PAINTING);
        }
        
        ObserverMessage msg;
        msg.visitor_id = visitor_id;
        msg.action = -1;
        msg.painting_id = -1;
        snprintf(msg.message, sizeof(msg.message), 
                "Посетитель %d прибыл к галерее", visitor_id);
        send_message(message_buffer, sem_message, msg);
    }
    
    ~Visitor() {
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            sem_close(sem_paintings[i]);
        }
        sem_close(sem_gallery_entry);
        sem_close(sem_gallery_exit);
        sem_close(sem_watcher);
        sem_close(sem_message);
        munmap(gallery_state, sizeof(GalleryState));
        munmap(message_buffer, sizeof(MessageBuffer));
    }
    
    void run() {
        // Попытка войти в галерею
        if (!enter_gallery()) {
            return;
        }
        
        // Осмотр картин
        view_paintings();
        
        // Выход из галереи
        exit_gallery();
    }
    
private:
    bool enter_gallery() {
        // Регистрируемся в очереди
        sem_wait(sem_watcher);
        gallery_state->waiting_visitors++;
        sem_post(sem_watcher);
        
        ObserverMessage msg;
        msg.visitor_id = visitor_id;
        msg.action = -1;
        msg.painting_id = -1;
        snprintf(msg.message, sizeof(msg.message), 
                "Посетитель %d в очереди", visitor_id);
        send_message(message_buffer, sem_message, msg);
        
        // Ждем разрешения войти (максимум 5 секунд)
        time_t start_time = time(nullptr);
        bool entered = false;
        
        while (time(nullptr) - start_time < 5) {
            if (sem_trywait(sem_gallery_entry) == 0) {
                entered = true;
                break;
            }
            usleep(100000); // Ждем 100ms перед следующей попыткой
        }
        
        if (entered) {
            msg.action = 0;
            snprintf(msg.message, sizeof(msg.message), 
                    "Посетитель %d вошел в галерею", visitor_id);
            send_message(message_buffer, sem_message, msg);
            return true;
        }
        
        // Если не удалось войти за 5 секунд
        msg.action = -1;
        snprintf(msg.message, sizeof(msg.message), 
                "Посетитель %d ушел - слишком долгое ожидание", visitor_id);
        send_message(message_buffer, sem_message, msg);
        
        sem_wait(sem_watcher);
        gallery_state->waiting_visitors--;
        sem_post(sem_watcher);
        return false;
    }
    
    void view_paintings() {
        std::uniform_int_distribution<int> dist(0, NUM_PAINTINGS - 1);
        std::uniform_real_distribution<double> time_dist(1.0, 3.0);
        
        for (int i = 0; i < NUM_PAINTINGS * 2; i++) {
            if (!gallery_state->simulation_active) break;
            
            int painting = dist(rng);
            
            // Пытаемся подойти к картине
            if (sem_trywait(sem_paintings[painting]) == 0) {
                sem_wait(sem_watcher);
                gallery_state->painting_counts[painting]++;
                sem_post(sem_watcher);
                
                ObserverMessage msg;
                msg.visitor_id = visitor_id;
                msg.action = 2;
                msg.painting_id = painting;
                snprintf(msg.message, sizeof(msg.message), 
                        "Посетитель %d рассматривает картину %d", visitor_id, painting);
                send_message(message_buffer, sem_message, msg);
                
                // Время осмотра
                double view_time = time_dist(rng);
                usleep(static_cast<int>(view_time * 1000000));
                
                // Отходим от картины
                sem_post(sem_paintings[painting]);
                sem_wait(sem_watcher);
                gallery_state->painting_counts[painting]--;
                sem_post(sem_watcher);
                
                msg.action = -1;
                snprintf(msg.message, sizeof(msg.message), 
                        "Посетитель %d отошел от картины %d", visitor_id, painting);
                send_message(message_buffer, sem_message, msg);
            } else {
                ObserverMessage msg;
                msg.visitor_id = visitor_id;
                msg.action = 3;
                msg.painting_id = painting;
                snprintf(msg.message, sizeof(msg.message), 
                        "Посетитель %d ждет у картины %d", visitor_id, painting);
                send_message(message_buffer, sem_message, msg);
                
                usleep(500000);
            }
            
            usleep(200000);
        }
    }
    
    void exit_gallery() {
        sem_wait(sem_gallery_exit);
        
        sem_wait(sem_watcher);
        gallery_state->current_visitors--;
        gallery_state->total_served++;
        sem_post(sem_watcher);
        
        sem_post(sem_gallery_exit);
        
        ObserverMessage msg;
        msg.visitor_id = visitor_id;
        msg.action = 1;
        msg.painting_id = -1;
        snprintf(msg.message, sizeof(msg.message), 
                "Посетитель %d покинул галерею", visitor_id);
        send_message(message_buffer, sem_message, msg);
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Использование: " << argv[0] << " <visitor_id>" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int visitor_id = std::atoi(argv[1]);
    Visitor visitor(visitor_id);
    visitor.run();
    
    return 0;
}