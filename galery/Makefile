# Makefile для симуляции картинной галереи
# Автор: [Ваше ФИО]
# Группа: [Ваша группа]

CXX = g++
CXXFLAGS = -std=c++11 -O2 -Wall -Wextra
LIBS = -lpthread

TARGETS = watcher visitor observer cleanup

all: $(TARGETS)

# Компиляция вахтера
watcher: watcher.cpp common.cpp common.h
	$(CXX) $(CXXFLAGS) -o $@ watcher.cpp common.cpp $(LIBS)

# Компиляция посетителя
visitor: visitor.cpp common.cpp common.h
	$(CXX) $(CXXFLAGS) -o $@ visitor.cpp common.cpp $(LIBS)

# Компиляция наблюдателя
observer: observer.cpp common.cpp common.h
	$(CXX) $(CXXFLAGS) -o $@ observer.cpp common.cpp $(LIBS)

# Компиляция утилиты очистки
cleanup: cleanup.cpp common.cpp common.h
	$(CXX) $(CXXFLAGS) -o $@ cleanup.cpp common.cpp $(LIBS)

# Очистка бинарных файлов и ресурсов
clean:
	rm -f $(TARGETS)
	./cleanup 2>/dev/null || true

# Запуск полной симуляции
run: all
	chmod +x run_simulation.sh
	./run_simulation.sh

# Запуск только вахтера
run-watcher: watcher
	./watcher

# Запуск наблюдателя (нужно указать ID)
run-observer: observer
	@if [ -z "$(id)" ]; then \
		echo "Использование: make run-observer id=1"; \
	else \
		./observer $(id); \
	fi

# Запуск посетителя (нужно указать ID)
run-visitor: visitor
	@if [ -z "$(id)" ]; then \
		echo "Использование: make run-visitor id=1"; \
	else \
		./visitor $(id); \
	fi

.PHONY: all clean run run-watcher run-observer run-visitor