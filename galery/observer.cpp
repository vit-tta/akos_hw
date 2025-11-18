#include "common.h"

class Observer {
private:
    GalleryState* gallery_state;
    MessageBuffer* message_buffer;
    sem_t* sem_message;
    bool active;
    int observer_id;
    
public:
    Observer(int id) : active(true), observer_id(id) {
        // Подключаемся к разделяемой памяти
        int shm_fd = shm_open(SHM_GALLERY_STATE, O_RDWR, 0666);
        gallery_state = (GalleryState*)mmap(NULL, sizeof(GalleryState), 
                                          PROT_READ | PROT_WRITE, MAP_SHARED, shm_fd, 0);
        
        int msg_fd = shm_open(SHM_MESSAGES, O_RDWR, 0666);
        message_buffer = (MessageBuffer*)mmap(NULL, sizeof(MessageBuffer), 
                                            PROT_READ | PROT_WRITE, MAP_SHARED, msg_fd, 0);
        
        // Открываем семафоры
        sem_message = sem_open(SEM_MESSAGE, 0);
        
        // Регистрируем наблюдателя
        sem_wait(sem_message);
        message_buffer->observer_count++;
        sem_post(sem_message);
        
        std::cout << "👁️ Наблюдатель " << observer_id << " начал работу\n";
    }
    
    ~Observer() {
        sem_wait(sem_message);
        message_buffer->observer_count--;
        sem_post(sem_message);
        
        munmap(gallery_state, sizeof(GalleryState));
        munmap(message_buffer, sizeof(MessageBuffer));
        sem_close(sem_message);
    }
    
    void run() {
        while (active && gallery_state->simulation_active) {
            process_messages();
            usleep(100000); // 100ms
        }
    }
    
private:
    void process_messages() {
        ObserverMessage msg;
        
        while (receive_message(message_buffer, sem_message, msg)) {
            display_message(msg);
        }
    }
    
    void display_message(const ObserverMessage& msg) {
        std::string prefix = "[Наблюдатель " + std::to_string(observer_id) + "] ";
        
        switch (msg.action) {
            case -1: // Информационное сообщение
                std::cout << prefix << "ℹ️ " << msg.message << std::endl;
                break;
            case 0: // Вошел в галерею
                std::cout << prefix << "✅ " << msg.message << std::endl;
                break;
            case 1: // Вышел из галереи
                std::cout << prefix << "🚪 " << msg.message << std::endl;
                break;
            case 2: // У картины
                std::cout << prefix << "🎨 " << msg.message << std::endl;
                break;
            case 3: // Ожидание у картины
                std::cout << prefix << "⏳ " << msg.message << std::endl;
                break;
            default:
                std::cout << prefix << msg.message << std::endl;
        }
    }
};

int main(int argc, char* argv[]) {
    if (argc != 2) {
        std::cerr << "Использование: " << argv[0] << " <observer_id>" << std::endl;
        return 1;
    }
    
    signal(SIGINT, signal_handler);
    signal(SIGTERM, signal_handler);
    
    int observer_id = std::atoi(argv[1]);
    Observer observer(observer_id);
    observer.run();
    
    return 0;
}