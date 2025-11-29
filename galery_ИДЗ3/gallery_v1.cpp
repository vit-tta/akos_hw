#include <iostream>
#include <vector>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>
#include <random>
#include <chrono>
#include <csignal>
#include <queue>
#include <map>

std::atomic<bool> stop_flag(false);
const int MAX_VISITORS = 25;
const int TOTAL_PAINTINGS = 5;
const int MAX_AT_PAINTING = 5;
const int TOTAL_VISITORS = 150;

void signal_handler(int signum) {
    std::cout << "\n🚨 Получен сигнал " << signum << ". Завершаем работу галереи..." << std::endl;
    stop_flag = true;
}

class GalleryModel {
private:
    std::vector<std::thread> visitors;
    std::thread guard_thread;
    std::mutex mtx;
    std::condition_variable cv;
    
    struct VisitorState {
        int id;
        bool in_gallery = false;
        bool viewing_painting = false;
        int current_painting = -1;
        std::vector<bool> viewed_paintings;
        bool finished = false;
    };
    
    std::vector<VisitorState> states;
    std::atomic<int> current_visitors{0};
    std::atomic<int> visitors_served{0};
    std::vector<int> visitors_at_painting;
    std::queue<int> waiting_queue;
    std::random_device rd;
    
public:
    GalleryModel() : visitors_at_painting(TOTAL_PAINTINGS, 0) {
        states.resize(TOTAL_VISITORS);
        for (int i = 0; i < TOTAL_VISITORS; i++) {
            states[i].id = i;
            states[i].viewed_paintings.resize(TOTAL_PAINTINGS, false);
        }
    }
    
    ~GalleryModel() {
        stop();
    }
    
    void start() {
        std::cout << "🎨 Запуск моделирования картинной галереи (4-6 баллов)" << std::endl;
        std::cout << "Максимум посетителей: " << MAX_VISITORS << std::endl;
        std::cout << "Всего картин: " << TOTAL_PAINTINGS << std::endl;
        std::cout << "Максимум у картины: " << MAX_AT_PAINTING << std::endl;
        std::cout << "Всего посетителей за день: " << TOTAL_VISITORS << std::endl;
        
        std::signal(SIGINT, signal_handler);
        std::signal(SIGTERM, signal_handler);
        
        guard_thread = std::thread(&GalleryModel::guard_worker, this);
        
        for (int i = 0; i < TOTAL_VISITORS; i++) {
            visitors.emplace_back(&GalleryModel::visitor_worker, this, i);
        }
        
        std::cout << "\n🎬 Галерея открыта! Нажмите Ctrl+C для закрытия\n" << std::endl;
        
        for (auto& thread : visitors) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (guard_thread.joinable()) {
            guard_thread.join();
        }
        
        print_results();
    }
    
    void stop() {
        stop_flag = true;
        cv.notify_all();
        
        for (auto& thread : visitors) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        
        if (guard_thread.joinable()) {
            guard_thread.join();
        }
    }
    
    void guard_worker() {
        std::cout << "👮 Вахтер начал работу" << std::endl;
        
        while (!stop_flag && visitors_served < TOTAL_VISITORS) {
            std::unique_lock<std::mutex> lock(mtx);
            
            // Пропускаем посетителей из очереди, если есть места
            while (!waiting_queue.empty() && current_visitors < MAX_VISITORS) {
                int visitor_id = waiting_queue.front();
                waiting_queue.pop();
                
                states[visitor_id].in_gallery = true;
                current_visitors++;
                
                std::cout << "✅ Вахтер пропустил посетителя " << visitor_id 
                          << " (в галерее: " << current_visitors << ")" << std::endl;
                
                cv.notify_all();
            }
            
            lock.unlock();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
        
        std::cout << "👮 Вахтер завершил работу" << std::endl;
    }
    
    void visitor_worker(int id) {
        std::mt19937 gen(rd() + id);
        std::uniform_int_distribution<> dis(500, 2000);
        
        // Посетитель приходит в случайное время
        int arrival_delay = dis(gen);
        std::this_thread::sleep_for(std::chrono::milliseconds(arrival_delay));
        
        if (stop_flag) return;
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            std::cout << "👤 Посетитель " << id << " пришел в галерею" << std::endl;
            
            if (current_visitors >= MAX_VISITORS) {
                waiting_queue.push(id);
                std::cout << "⏳ Посетитель " << id << " встал в очередь (очередь: " 
                          << waiting_queue.size() << ")" << std::endl;
            } else {
                states[id].in_gallery = true;
                current_visitors++;
                std::cout << "✅ Посетитель " << id << " вошел в галерею (всего: " 
                          << current_visitors << ")" << std::endl;
            }
        }
        
        // Ожидание входа в галерею
        std::unique_lock<std::mutex> lock(mtx);
        cv.wait(lock, [&]() { return states[id].in_gallery || stop_flag; });
        lock.unlock();
        
        if (stop_flag) return;
        
        // Осмотр картин
        while (!stop_flag && !states[id].finished) {
            int painting = select_painting(id, gen);
            if (painting == -1) {
                // Все картины просмотрены
                states[id].finished = true;
                break;
            }
            
            view_painting(id, painting, gen);
            if (stop_flag) break;
        }
        
        // Выход из галереи
        {
            std::lock_guard<std::mutex> lock(mtx);
            states[id].in_gallery = false;
            current_visitors--;
            visitors_served++;
            std::cout << "🚪 Посетитель " << id << " покинул галерею (осталось: " 
                      << current_visitors << ")" << std::endl;
        }
        
        cv.notify_all();
    }
    
    int select_painting(int visitor_id, std::mt19937& gen) {
        std::vector<int> available_paintings;
        
        for (int i = 0; i < TOTAL_PAINTINGS; i++) {
            if (!states[visitor_id].viewed_paintings[i]) {
                available_paintings.push_back(i);
            }
        }
        
        if (available_paintings.empty()) {
            return -1;
        }
        
        std::uniform_int_distribution<> dis(0, available_paintings.size() - 1);
        return available_paintings[dis(gen)];
    }
    
    void view_painting(int visitor_id, int painting, std::mt19937& gen) {
        // Ожидание, если у картины слишком много посетителей
        {
            std::unique_lock<std::mutex> lock(mtx);
            cv.wait(lock, [&]() { 
                return visitors_at_painting[painting] < MAX_AT_PAINTING || stop_flag; 
            });
            
            if (stop_flag) return;
            
            visitors_at_painting[painting]++;
            states[visitor_id].current_painting = painting;
            states[visitor_id].viewing_painting = true;
            
            std::cout << "🖼️ Посетитель " << visitor_id << " подошел к картине " << painting 
                      << " (у картины: " << visitors_at_painting[painting] << ")" << std::endl;
        }
        
        // Осмотр картины
        int view_time = 1000 + (gen() % 2000); // 1-3 секунды
        std::this_thread::sleep_for(std::chrono::milliseconds(view_time));
        
        if (stop_flag) return;
        
        {
            std::lock_guard<std::mutex> lock(mtx);
            visitors_at_painting[painting]--;
            states[visitor_id].viewing_painting = false;
            states[visitor_id].viewed_paintings[painting] = true;
            
            std::cout << "✅ Посетитель " << visitor_id << " осмотрел картину " << painting 
                      << " (осталось у картины: " << visitors_at_painting[painting] << ")" << std::endl;
        }
        
        cv.notify_all();
        
        // Пауза между картинами
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
    }
    
    void print_results() {
        std::cout << "\n📊 ИТОГОВЫЕ РЕЗУЛЬТАТЫ РАБОТЫ ГАЛЕРЕИ:" << std::endl;
        std::cout << "Обслужено посетителей: " << visitors_served << " из " << TOTAL_VISITORS << std::endl;
        std::cout << "Максимальное количество посетителей одновременно: " << MAX_VISITORS << std::endl;
        std::cout << "Размер очереди на входе: " << waiting_queue.size() << std::endl;
    }
};

int main() {
    try {
        GalleryModel model;
        model.start();
        std::cout << "\n🎉 Работа галереи завершена (4-6 баллов)!" << std::endl;
    } catch (const std::exception& e) {
        std::cerr << "❌ Ошибка: " << e.what() << std::endl;
        return 1;
    }
    
    return 0;
}