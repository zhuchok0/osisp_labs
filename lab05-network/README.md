# Лабораторная работа №5. Вариант 10 — удаленный интерпретатор операций с файлами

## Назначение
TCP-сервер выполняет **только чтение** файлов из каталога `sandbox` по текстовым командам клиента:
`LIST`, `SIZE <file>`, `COUNT_LINES <file>`, `READ_FIRST <file> <n>`, `HELP`.
Изменение и удаление файлов не предусмотрено. Выход за пределы `sandbox` запрещен.

## Окружение
Windows 10/11, Visual Studio 2019+ (MSVC, C++17) или CMake 3.16+; Python 3.8+ для скриптов (только стандартная библиотека).

## Транспорт
TCP, Windows Sockets 2 (WSAStartup, socket, bind, listen, accept, connect, send, recv, select, shutdown, closesocket, WSACleanup).
Сервер слушает только `127.0.0.1` по умолчанию. Порт: `20000 + STUDENT_ID`.

## Сборка
Нужен компилятор: MSVC (`cl`, запускать из «Developer Command Prompt for VS») или MinGW-w64 (`g++`).

    python scripts/build.py

Скрипт сам выбирает `cl` или `g++` и кладет `server.exe`, `client.exe` в `build\`.
CMake необязателен; `CMakeLists.txt` оставлен как альтернатива (`cmake -B build && cmake --build build --config Release`,
файлы окажутся в `build\Release\` — скрипты находят их сами или через `--bin build/Release`).

## Подготовка данных
    python scripts/gen_data.py --student-id 10

Создает `sandbox\` (файлы 1 КБ, 100 КБ, 10 МБ, 50 МБ, `empty.txt`, `noeol.txt`) с `SEED = 1000 + STUDENT_ID*97`
и `secret.txt` вне sandbox (для проверки запрета выхода).

## Запуск сервера
    build\server.exe --host 127.0.0.1 --port 20010 --sandbox sandbox [--timeout 30000] [--delay 0]

`--timeout` — тайм-аут простоя клиента, мс; `--delay` — искусственная задержка ответа (только для теста тайм-аутов).
Штатное завершение — Ctrl+C (дожидается потоков, закрывает сокеты, вызывает WSACleanup).

## Запуск клиента
    build\client.exe --host 127.0.0.1 --port 20010 --id 1                     (интерактивно)
    build\client.exe --host 127.0.0.1 --port 20010 --id 1 --cmd "SIZE f_1KB.txt" --count 20 --timeout 5000 --quiet --csv data\results.csv --label cfg

Код возврата: 0 — без сбоев; 1 — тайм-аут/обрыв; 2 — сервер недоступен.

## Протокол (версия V1)
Кадр: `[Length: 4 байта, uint32, big-endian][Payload: Length байт]`, 1 ≤ Length ≤ 65536, UTF-8.

    Запрос: V1 <requestId> <clientId> <COMMAND> [args...]
    Ответ:  V1 <requestId> <OK|ERR> <code> <server_us>\n<body>

| Код | Значение |
|-----|----------|
| 200 | OK |
| 400 | неизвестная команда, неверные аргументы, некорректный запрос |
| 403 | недопустимый путь (выход за sandbox и эквивалентные формы) |
| 404 | файл не найден |
| 413 | некорректная длина кадра (соединение закрывается) |
| 500 | внутренняя ошибка |
| 503 | слишком много клиентов (>64) |

`server_us` — время обработки на сервере (без сети), по QueryPerformanceCounter.
Ограничения: `READ_FIRST` — n от 1 до 1000 и не более 60000 байт ответа.

## Защита sandbox
1. Белый список имени: только `[A-Za-z0-9._-]`, длина ≤ 100, без ведущей/завершающей точки, без `..`, без имен устройств (CON, NUL, COM1…). Отсекает `../`, `..\`, `C:\…`, `\\host\share`, `/etc/passwd`, `file:stream`, `%2e%2e%2f`, вложенные каталоги.
2. Второй рубеж: `canonical()` + проверка `equivalent(parent, sandbox)`, запрет symlink и не-обычных файлов.

## Примеры
    LIST
    SIZE f_1KB.txt
    COUNT_LINES f_10MB.txt
    READ_FIRST f_100KB.txt 5
    SIZE ..\secret.txt          ->  ERR 403
    SIZE missing.txt            ->  ERR 404

## Тесты
    python tests/run_tests.py --student-id 10

Проверяет: отсутствующий файл, 13 форм некорректного пути, неверные аргументы, неизвестную команду, неверную длину кадра, частичную отправку, тайм-аут клиента, недоступный сервер, 3 одновременных клиента.

## Эксперименты
    python scripts/run_experiments.py --student-id 10 --count 20

Конфигурации: 1 и 3 клиента × файлы 1 КБ/100 КБ/10 МБ/50 МБ × SIZE/COUNT_LINES/READ_FIRST × 3 запуска.
Результат: `data\results.csv` (время подключения, полное время, средний RTT, среднее серверное время, байты), `data\server.log`.
Графики строятся по CSV: (1) средний RTT и серверное время от размера файла для 1 и 3 клиентов; (2) полное время/пропускная способность от числа клиентов.
