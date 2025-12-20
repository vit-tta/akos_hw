.data
DISPLAY_ADDR:    .word 0x10008000
field:          .space 1024     # 32x32 = 1024 байта
field_next:     .space 1024
filename:       .space 64
input_buffer:   .space 1024

msg_menu:       .asciz "1-Консоль, 2-Файл: "
msg_file:       .asciz "Имя файла: "
msg_start:      .asciz "Старт! (Нажмите Stop для выхода)\n"
msg_gen:        .asciz "Поколение: "

.text
.globl main

main:
    # Выбор ввода
    li a7, 4
    la a0, msg_menu
    ecall
    
    li a7, 5
    ecall
    
    # Если не 2, используем встроенный паттерн
    li t0, 2
    bne a0, t0, default_pattern
    
    # Ввод из файла
    li a7, 4
    la a0, msg_file
    ecall
    
    li a7, 8
    la a0, filename
    li a1, 64
    ecall
    
    # Открытие файла
    li a7, 1024
    la a0, filename
    li a1, 0
    ecall
    bltz a0, default_pattern  # если ошибка - используем паттерн по умолчанию
    
    mv s0, a0
    li a7, 63
    mv a0, s0
    la a1, input_buffer
    li a2, 1024
    ecall
    
    li a7, 57
    mv a0, s0
    ecall
    
    # Заполняем поле из файла
    la a0, field
    la a1, input_buffer
    jal load_from_buffer
    j start_game

default_pattern:
    # Создаем паттерн "планер" в центре
    la a0, field
    li a1, 1024
    jal clear_field
    
    # Планер в координатах (15,15)
    la t0, field
    
    # Координаты планера
    li t1, 15          # y
    slli t1, t1, 5     # y * 32
    addi t1, t1, 15    # + x = 15
    add t0, t0, t1
    
    li t2, 1
    sb t2, 0(t0)       # (15,15)
    sb t2, 2(t0)       # (15,17)
    
    addi t0, t0, 32    # следующая строка y=16
    sb t2, 1(t0)       # (16,16)
    sb t2, 2(t0)       # (16,17)
    
    addi t0, t0, 32    # y=17
    sb t2, 1(t0)       # (17,16)

start_game:
    li a7, 4
    la a0, msg_start
    ecall
    
    # Главный игровой цикл
    li s10, 0          # счетчик поколений
    
game_loop:
    # Отрисовка текущего состояния
    la a0, field
    jal draw_field
    
    # Вывод номера поколения
    li a7, 4
    la a0, msg_gen
    ecall
    
    li a7, 1
    mv a0, s10
    ecall
    
    li a7, 11
    li a0, '\n'
    ecall
    
    # Вычисление следующего поколения
    la a0, field
    la a1, field_next
    jal next_generation
    
    # Копирование обратно
    la a0, field
    la a1, field_next
    li a2, 1024
    jal copy_memory
    
    # Задержка
    li a0, 300
    li a7, 32
    ecall
    
    addi s10, s10, 1
    j game_loop

# Очистка поля
clear_field:
    mv t0, a0
    li t1, 0
clear_loop:
    sb zero, 0(t0)
    addi t0, t0, 1
    addi t1, t1, 1
    blt t1, a1, clear_loop
    ret

# Загрузка из буфера
load_from_buffer:
    mv t0, a0      # field
    mv t1, a1      # buffer
    li t2, 0       # счетчик
    
load_loop:
    lb t3, 0(t1)
    beqz t3, load_done
    
    # Преобразование '0'/'1' в 0/1
    li t4, '1'
    bne t3, t4, not_one
    li t3, 1
    j store_cell
not_one:
    li t3, 0
    
store_cell:
    sb t3, 0(t0)
    
    addi t0, t0, 1
    addi t1, t1, 1
    addi t2, t2, 1
    li t4, 1024
    blt t2, t4, load_loop
    
load_done:
    ret

# Отрисовка поля
draw_field:
    lw t0, DISPLAY_ADDR
    li t1, 0        # y
    
draw_y:
    li t2, 0        # x
    
draw_x:
    # Адрес в field[y][x]
    slli t3, t1, 5    # y * 32
    add t3, t3, t2    # + x
    add t3, a0, t3    # + base
    lb t4, 0(t3)      # значение клетки
    
    # Адрес пикселя на экране
    slli t3, t1, 7    # y * 128 (32*4)
    slli t5, t2, 2    # x * 4
    add t3, t3, t5
    add t3, t0, t3
    
    # Цвет: белый для живых, черный для мертвых
    beqz t4, draw_black
    li t5, 0x00FFFFFF  # белый
    sw t5, 0(t3)
    j draw_next
    
draw_black:
    li t5, 0xFF000000  # черный (с альфой)
    sw t5, 0(t3)
    
draw_next:
    addi t2, t2, 1
    li t5, 32
    blt t2, t5, draw_x
    
    addi t1, t1, 1
    li t5, 32
    blt t1, t5, draw_y
    
    ret

# Подсчет соседей
# a0 - field, a1 - x, a2 - y
count_neighbors:
    li t0, 0        # счетчик
    li t1, -1       # dx
    
count_dx:
    li t2, -1       # dy
    
count_dy:
    # Пропускаем центр
    beqz t1, check_dy
    j do_count
check_dy:
    beqz t2, skip_cell
    
do_count:
    # Координаты соседа
    add t3, a1, t1   # nx = x + dx
    add t4, a2, t2   # ny = y + dy
    
    # Тороидальность
    li t5, 32
    bgez t3, not_neg_x
    add t3, t3, t5
not_neg_x:
    blt t3, t5, not_big_x
    sub t3, t3, t5
not_big_x:
    
    bgez t4, not_neg_y
    add t4, t4, t5
not_neg_y:
    blt t4, t5, not_big_y
    sub t4, t4, t5
not_big_y:
    
    # Получаем значение клетки
    slli t5, t4, 5    # ny * 32
    add t5, t5, t3    # + nx
    add t5, a0, t5
    lb t6, 0(t5)
    
    beqz t6, skip_cell
    addi t0, t0, 1
    
skip_cell:
    addi t2, t2, 1
    li t5, 1
    ble t2, t5, count_dy
    
    addi t1, t1, 1
    li t5, 1
    ble t1, t5, count_dx
    
    mv a0, t0
    ret

# Следующее поколение
# a0 - field, a1 - next
next_generation:
    mv s0, a0        # текущее
    mv s1, a1        # следующее
    li s2, 0         # y
    
next_y:
    li s3, 0         # x
    
next_x:
    # Текущее состояние
    slli t0, s2, 5    # y * 32
    add t0, t0, s3    # + x
    add t0, s0, t0
    lb t1, 0(t0)      # текущее значение
    
    # Сохраняем в стек
    addi sp, sp, -16
    sw t1, 0(sp)
    sw s2, 4(sp)
    sw s3, 8(sp)
    sw ra, 12(sp)
    
    # Считаем соседей
    mv a0, s0
    mv a1, s3
    mv a2, s2
    jal count_neighbors
    
    # Восстанавливаем
    lw t1, 0(sp)
    lw s2, 4(sp)
    lw s3, 8(sp)
    lw ra, 12(sp)
    addi sp, sp, 16
    
    mv t2, a0        # кол-во соседей
    
    # Правила игры
    beqz t1, dead_cell
    
    # Живая клетка
    li t0, 2
    blt t2, t0, die
    li t0, 3
    bgt t2, t0, die
    j live
    
dead_cell:
    # Мертвая клетка
    li t0, 3
    bne t2, t0, die
    # Оживает
    j live
    
die:
    li t3, 0
    j store_next
    
live:
    li t3, 1
    
store_next:
    # Сохраняем в next
    slli t0, s2, 5    # y * 32
    add t0, t0, s3    # + x
    add t0, s1, t0
    sb t3, 0(t0)
    
    # Следующая клетка
    addi s3, s3, 1
    li t0, 32
    blt s3, t0, next_x
    
    addi s2, s2, 1
    li t0, 32
    blt s2, t0, next_y
    
    ret

# Копирование памяти
copy_memory:
    li t0, 0
copy_loop:
    bge t0, a2, copy_done
    lb t1, 0(a1)
    sb t1, 0(a0)
    addi a0, a0, 1
    addi a1, a1, 1
    addi t0, t0, 1
    j copy_loop
copy_done:
    ret

# Конец программы
end_program:
    li a7, 10
    ecall