#include "common.h"

class Watcher {
private:
    GalleryState* gallery_state;
    MessageBuffer* message_buffer;
    sem_t* sem_gallery_entry;
    sem_t* sem_gallery_exit;
    sem_t* sem_watcher;
    sem_t* sem_message;
    
public:
    Watcher() {
        // Создаем разделяемую память для состояния галереи
        int shm_fd = shm_open(SHM_GALLERY_STATE, O_CREAT | O_RDWR, 0666);
        ftruncate(shm_fd, sizeof(GalleryState));
        gallery_state = (GalleryState*)mmap(NULL, sizeof(GalleryState), 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        
        // Создаем разделяемую память для сообщений
        int msg_fd = shm_open(SHM_MESSAGES, O_CREAT | O_RDWR, 0666);
        ftruncate(msg_fd, sizeof(MessageBuffer));
        message_buffer = (MessageBuffer*)mmap(NULL, sizeof(MessageBuffer), 
                                            PROT_READ | PROT_WRITE, MAP_SHARED, msg_fd, 0);
        
        // Инициализация состояния галереи
        gallery_state->current_visitors = 0;
        gallery_state->total_served = 0;
        gallery_state->waiting_visitors = 0;
        gallery_state->simulation_active = true;
        gallery_state->message_count = 0;
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            gallery_state->painting_counts[i] = 0;
        }
        
        // Инициализация буфера сообщений
        message_buffer->read_index = 0;
        message_buffer->write_index = 0;
        message_buffer->observer_count = 0;
        
        // Создаем семафоры
        sem_gallery_entry = sem_open(SEM_GALLERY_ENTRY, O_CREAT, 0666, 1);
        sem_gallery_exit = sem_open(SEM_GALLERY_EXIT, O_CREAT, 0666, 1);
        sem_watcher = sem_open(SEM_WATCHER, O_CREAT, 0666, 1);
        sem_message = sem_open(SEM_MESSAGE, O_CREAT, 0666, 1);
        
        std::cout << "🚨 Вахтер начал работу. Галерея открыта!\n";
    }
    
    ~Watcher() {
        cleanup();
    }
    
    void run() {
        time_t start_time = time(nullptr);
        
        while (gallery_state->simulation_active) {
            // Проверяем время работы (30 секунд)
            if (time(nullptr) - start_time >= SIMULATION_TIME) {
                gallery_state->simulation_active = false;
                
                ObserverMessage msg;
                msg.visitor_id = -1;
                msg.action = -1;
                msg.painting_id = -1;
                snprintf(msg.message, sizeof(msg.message), 
                        "🕒 Вахтер: время работы истекло, закрываем галерею.");
                send_message(message_buffer, sem_message, msg);
                
                std::cout << "🕒 Вахтер: время работы истекло, закрываем галерею.\n";
                break;
            }
            
            // Обрабатываем посетителей
            process_visitors();
            
            sleep(1);
            
            // Выводим текущее состояние
            print_status();
        }
        
        // Завершаем работу
        std::cout << "🏁 Вахтер завершил работу. Обслужено посетителей: " 
                  << gallery_state->total_served << std::endl;
    }
    
private:
    void process_visitors() {
        sem_wait(sem_watcher);
        
        // Пропускаем посетителей если есть место
        if (gallery_state->waiting_visitors > 0 && 
            gallery_state->current_visitors < MAX_VISITORS) {
            
            int can_enter = std::min(gallery_state->waiting_visitors,
                                   MAX_VISITORS - gallery_state->current_visitors);
            
            for (int i = 0; i < can_enter; i++) {
                sem_post(sem_gallery_entry);
                gallery_state->waiting_visitors--;
                gallery_state->current_visitors++;
                
                ObserverMessage msg;
                msg.visitor_id = -1; // системное сообщение
                msg.action = 0;
                msg.painting_id = -1;
                snprintf(msg.message, sizeof(msg.message), 
                        "Вахтер пропустил посетителя. В галерее: %d/%d", 
                        gallery_state->current_visitors, MAX_VISITORS);
                send_message(message_buffer, sem_message, msg);
            }
        }
        
        sem_post(sem_watcher);
    }
    
    void print_status() {
        std::cout << "📊 Статус: " << gallery_state->current_visitors << "/" 
                  << MAX_VISITORS << " посетителей | Ожидание: " 
                  << gallery_state->waiting_visitors 
                  << " | Всего обслужено: " << gallery_state->total_served << std::endl;
        
        std::cout << "🎨 У картин: ";
        for (int i = 0; i < NUM_PAINTINGS; i++) {
            std::cout << gallery_state->painting_counts[i] << " ";
        }
        std::cout << std::endl;
    }
    
    void cleanup() {
        munmap(gallery_state, sizeof(GalleryState));
        munmap(message_buffer, sizeof(MessageBuffer));
        shm_unlink(SHM_GALLERY_STATE);
        shm_unlink(SHM_MESSAGES);
        
        sem_close(sem_gallery_entry);
        sem_close(sem_gallery_exit);
        sem_close(sem_watcher);
        sem_close(sem_message);
    }
};

int main() {
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    Watcher watcher;
    watcher.run();
    
    return 0;
}