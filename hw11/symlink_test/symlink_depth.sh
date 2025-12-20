#!/bin/bash

TEMP_DIR="./symlink_test_dir"
MAX_DEPTH=100

cleanup() {
    echo "Очистка временных файлов..."
    rm -rf "$TEMP_DIR"
}

trap cleanup EXIT INT TERM

mkdir -p "$TEMP_DIR" || {
    echo "Ошибка создания директории $TEMP_DIR" >&2
    exit 1
}

cd "$TEMP_DIR" || {
    echo "Ошибка перехода в директорию $TEMP_DIR" >&2
    exit 1
}

# Создаем исходный файл
touch "a"
echo "Создан исходный файл: a"

depth=0
current="a"

while [ $depth -lt $MAX_DEPTH ]; do
    # Создаем имя для новой ссылки
    new_link="link${depth}"
    
    # Создаем символьную ссылку
    if ! ln -s "$current" "$new_link" 2>/dev/null; then
        echo "Ошибка создания символьной ссылки $new_link -> $current" >&2
        break
    fi
    
    echo "Создана символьная ссылка: $new_link -> $current"
    
    # Пытаемся открыть файл
    if output=$(cat "$new_link" 2>&1); then
        # Успешно открыли
        current="$new_link"
        depth=$((depth + 1))
    else
        # Проверяем, это ли ошибка ELOOP
        if echo "$output" | grep -q "Too many levels of symbolic links\|ELOOP"; then
            echo ""
            echo "Достигнута максимальная глубина рекурсии!"
            echo "Глубина рекурсии: $depth символьных ссылок"
            break
        else
            echo "Ошибка открытия $new_link: $output" >&2
            break
        fi
    fi
done

if [ $depth -eq $MAX_DEPTH ]; then
    echo "Достигнут максимальный предел проверки ($MAX_DEPTH ссылок)"
fi

echo "Итоговая глубина: $depth"
exit 0