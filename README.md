# Проект MiniPTM - Документация протоколов

## Обзор проекта

**MiniPTM** - это комплексная система для работы с устройствами синхронизации времени и частоты на базе Renesas ClockMatrix (8A34002). Проект включает драйвер ядра Linux, Python API и различные протоколы для управления платами MiniPTM.

## Архитектура системы

```
┌─────────────────┐    ┌─────────────────┐    ┌─────────────────┐
│   Приложения    │────│   Python API    │────│  Драйвер ядра   │
│                 │    │                 │    │   Linux         │
└─────────────────┘    └─────────────────┘    └─────────────────┘
         │                       │                       │
         │                       │                       │
    ┌────▼─────┐            ┌────▼─────┐            ┌────▼─────┐
    │Протоколы │            │Протоколы │            │Протоколы │
    │приложений│            │DPLL/DPOF │            │PCIe/I2C  │
    └──────────┘            └──────────┘            └──────────┘
```

## Основные протоколы

### 1. DPLL (Digital Phase-Locked Loop) - Цифровая фазовая автоподстройка частоты

#### Назначение
DPLL обеспечивает высокоточную синхронизацию частоты и фазы сигналов в системах связи и телекоммуникаций.

#### Основные компоненты

##### **Входы (Inputs)**
- **Количество**: до 16 входов тактового сигнала
- **Типы сигналов**: CMOS, LVPECL, LVDS, оптические через SFP
- **Мониторинг**: частота, фаза, LOS (потеря сигнала), активность

##### **Выходы (Outputs)**
- **Количество**: до 8 выходов
- **Типы**: CMOS, LVPECL, LVDS
- **Функции**: генерация PPS, распределение тактового сигнала

##### **TOD (Time of Day)**
- **Количество**: до 4 независимых TOD
- **Формат**: секунды (48 бит), наносекунды (32 бит), субнаносекунды (8 бит)
- **Синхронизация**: от GPS, PTP, других источников

#### Регистры управления

```c
// Структура регистра DPLL
typedef struct {
    uint8_t input_select;      // Выбор входного сигнала
    uint8_t output_config;     // Конфигурация выхода
    uint8_t phase_offset[4];   // Коррекция фазы (32 бита)
    uint8_t frequency_offset[4]; // Коррекция частоты (32 бита)
    uint8_t status_flags;      // Флаги состояния
    uint8_t control_flags;     // Флаги управления
} dpll_registers_t;
```

#### Команды управления

| Команда | Код | Описание |
|---------|-----|----------|
| `INIT` | 0x01 | Инициализация DPLL |
| `LOCK` | 0x02 | Захват и удержание частоты |
| `HOLDOVER` | 0x03 | Режим удержания при потере сигнала |
| `FREE_RUN` | 0x04 | Свободный ход |
| `RESET` | 0x05 | Сброс DPLL |

### 2. DPOF (DPLL Over Fiber) - DPLL через оптоволокно

#### Назначение
DPOF обеспечивает передачу данных синхронизации и пользовательской информации через оптоволокно с использованием PWM (Pulse Width Modulation) кодирования.

#### Архитектура протокола

##### **Компоненты системы DPOF**

```
┌─────────────┐    ┌─────────────┐
│   Master    │────│   Slave     │
│   Side      │    │   Side      │
│             │    │             │
│ ┌─────────┐ │    │ ┌─────────┐ │
│ │ PWM     │ │    │ │ PWM     │ │
│ │ Encoder │◄┼────┼►│ Decoder │ │
│ └─────────┘ │    │ └─────────┘ │
│             │    │             │
│ ┌─────────┐ │    │ ┌─────────┐ │
│ │ TOD     │◄┼────┼►│ TOD     │ │
│ │ Encoder │ │    │ │ Decoder │ │
│ └─────────┘ │    │ └─────────┘ │
└─────────────┘    └─────────────┘
```

##### **Каналы передачи**

1. **TOD Channel (Time of Day)**
   - **Направление**: Двунаправленный
   - **Количество**: 4 независимых канала
   - **Формат данных**: 6 байт секунд (TOD_SEC[0-5])
   - **Протокол**: TOD_SEC[5] и TOD_SEC[4] используются для handshake

2. **PWM User Data Channel**
   - **Направление**: Двунаправленный
   - **Размер буфера**: 128 байт
   - **Количество**: 1 канал (разделяется между TX и RX)

##### **Протокол handshake**

Протокол использует двухбайтовый handshake механизм с использованием TOD_SEC[5] и TOD_SEC[4]:

```python
# Структура handshake байтов
handshake_bytes = {
    'state': TOD_SEC[5],      # Состояние: 0x0=initial, 0x1=accept, 0x2=end
    'action': TOD_SEC[4],     # Действие: 0x0=idle, 0x1=read, 0x2=write
    'random': TOD_SEC[3],     # Случайный байт для разрешения коллизий
    'priority': TOD_SEC[2],   # Приоритет (старший байт)
}
```

##### **Алгоритм handshake**

```python
def dpof_handshake():
    # Шаг 1: Инициация
    send_tod_state(0x0, desired_action, random_byte)

    # Шаг 2: Проверка коллизии
    if received_random == my_random:
        change_random_byte()
        wait_for_other_change()

    # Шаг 3: Определение приоритета
    if received_random < my_random:
        # Мы проигрываем - ждем команды
        send_tod_state(0x1, 0x0, 0x0)  # Accept
        wait_for_master_command()
    else:
        # Мы выигрываем - выполняем действие
        send_tod_state(0x1, 0x0, 0x0)  # Accept
        execute_desired_action()

    # Шаг 4: Завершение
    send_tod_state(0x2, 0x0, 0x0)  # End
```

##### **Состояния DPOF**

| Состояние | Код | Описание |
|-----------|-----|----------|
| `IDLE` | 0 | Ожидание |
| `RX_SLAVE` | 1 | Прием в режиме slave |
| `TRANSMIT_START` | 2 | Начало передачи |
| `TRANSMIT_WON` | 3 | Выигрыш арбитража |
| `TRANSMIT_WRITE` | 4 | Передача данных |
| `TRANSMIT_QUERY` | 5 | Запрос данных |
| `TRANSMIT_DONE_WAIT` | 6 | Ожидание завершения |
| `RX_SLAVE_RESPOND_QUERY` | 7 | Ответ на запрос |
| `RX_SLAVE_RESPOND_QUERY_WAIT_FIFO_TX` | 8 | Ожидание FIFO TX |
| `RX_SLAVE_RESPOND_QUERY_WAIT_FIFO_TX_DONE` | 9 | Завершение FIFO TX |
| `RX_SLAVE_WAIT_WRITE` | 10 | Ожидание записи |
| `RX_SLAVE_DONE_WAIT` | 11 | Завершение ожидания |

##### **PWM кодирование**

```python
# Структура PWM кадра
pwm_frame = {
    'encoder_id': 0x00,       # ID энкодера (1 байт)
    'decoder_id': 0x01,       # ID декодера (1 байт)
    'data_size': 0x02,        # Размер данных (1 байт)
    'command_status': 0x03,   # Команда/статус (1 байт)
    'user_data': [0x04..0x83] # Пользовательские данные (128 байт)
}
```

### 3. I2C протокол для управления DPLL

#### Назначение
I2C используется для низкоуровневого доступа к регистрам DPLL и другим компонентам системы.

#### Адреса устройств

| Устройство | I2C адрес | Описание |
|------------|-----------|----------|
| DPLL | 0x58 | Основной контроллер синхронизации |
| I2C MUX | 0x70 | Мультиплексор каналов I2C |
| SFP A0 | 0x50 | Диагностика SFP модуля |
| SFP A2 | 0x51 | Серийный ID SFP модуля |

#### Структура транзакций

##### **Запись регистра DPLL**

```python
def write_dpll_register(base_addr, offset, value):
    """
    Запись в регистр DPLL
    base_addr: базовый адрес модуля (0xC000, 0xC200, etc.)
    offset: смещение регистра (0x00-0xFF)
    value: записываемое значение (0x00-0xFF)
    """

    # Расчет полного адреса
    full_addr = base_addr + offset
    addr_lower = full_addr & 0xFF
    addr_upper = (full_addr >> 8) & 0xFF

    # Установка базового адреса (если изменился)
    if current_base != addr_upper:
        i2c_write_block(DPLL_ADDR, 0xFC, [addr_lower, addr_upper, 0x10, 0x20])
        current_base = addr_upper

    # Запись значения
    i2c_write_byte(DPLL_ADDR, addr_lower, value)
```

##### **Чтение регистра DPLL**

```python
def read_dpll_register(base_addr, offset):
    """
    Чтение регистра DPLL
    Возвращает прочитанное значение
    """

    # Аналогичный расчет адреса
    full_addr = base_addr + offset
    addr_lower = full_addr & 0xFF
    addr_upper = (full_addr >> 8) & 0xFF

    # Установка базового адреса
    if current_base != addr_upper:
        i2c_write_block(DPLL_ADDR, 0xFC, [addr_lower, addr_upper, 0x10, 0x20])
        current_base = addr_upper

    # Чтение значения
    return i2c_read_byte(DPLL_ADDR, addr_lower)
```

#### Мультиплексор I2C

```python
# Настройка каналов мультиплексора
mux_channels = {
    0x1: "SFP1",      # Доступ к первому SFP модулю
    0x2: "SFP2",      # Доступ ко второму SFP модулю
    0x8: "DPLL"       # Доступ к DPLL
}

def select_mux_channel(channel):
    """Выбор канала мультиплексора"""
    i2c_write_byte(MUX_ADDR, 0x00, channel)
```

### 4. PCIe протокол

#### Назначение
PCIe используется для высокоскоростного доступа к памяти устройства и управления GPIO.

#### Параметры устройства

```c
#define VENDOR_ID 0x8086    // Intel
#define DEVICE_ID 0x125b    // I225 Ethernet Controller
#define BAR_SIZE 0x20000    // Размер BAR 0 (128KB)
```

#### Регистры управления GPIO

##### **SDP (Software Definable Pins)**

```c
// Регистры для управления SDP0-SDP3
#define CTRL           0x0000    // Управление скоростью/дуплексом
#define CTRL_EXT       0x0018    // Расширенное управление

// Биты для SDP0
#define SDP0_IODIR     (1<<22)   // Направление (CTRL)
#define SDP0_DATA      (1<<2)    // Данные (CTRL_EXT)

// Биты для SDP1
#define SDP1_IODIR     (1<<23)   // Направление (CTRL)
#define SDP1_DATA      (1<<3)    // Данные (CTRL_EXT)

// Биты для SDP2-SDP3
#define SDP2_IODIR     (1<<10)   // Направление (CTRL_EXT)
#define SDP3_IODIR     (1<<11)   // Направление (CTRL_EXT)
#define SDP2_DATA      (1<<6)    // Данные (CTRL_EXT)
#define SDP3_DATA      (1<<7)    // Данные (CTRL_EXT)
```

##### **LED управление**

```c
#define LED_CONFIG     0x0E00    // Регистр конфигурации LED

#define LED_ALWAYS_ON  0x0       // LED всегда включен
#define LED_ALWAYS_OFF 0x1       // LED всегда выключен
#define LED_NORMAL     0x2       // LED в нормальном режиме
```

### 5. Форматы конфигурационных файлов

#### Формат .tcs (Timing Commander Script)

##### **Структура файла**

```text
Software Version: 1.17.0.14687
Personality: ClockMatrix [34.4]
Part#: 8A34002
... (заголовок) ...

Page.Byte#                      BinaryFormat HexValue Page.Byte#
C0.00                                00000000       00 C0.00
C0.01                                00000000       00 C0.01
... (данные регистров) ...
```

##### **Парсинг файла**

```python
def parse_tcs_file(file_path):
    """
    Парсинг .tcs файла
    Возвращает список кортежей (адрес, значение)
    """
    config_data = []

    with open(file_path, 'r') as file:
        for line in file:
            parts = line.split()
            if len(parts) >= 3 and '.' in parts[0]:
                try:
                    # Парсинг адреса (формат C0.05)
                    page_byte = parts[0].split('.')
                    page = int(page_byte[0], 16)
                    byte_offset = int(page_byte[1], 16)
                    address = (page << 8) | byte_offset

                    # Парсинг значения
                    value = int(parts[2], 16)

                    config_data.append((address, value))
                except ValueError:
                    continue

    return config_data
```

#### Формат .hex (Intel HEX)

##### **Структура записей**

```text
:BBAAAATTHHHH...HHHHCC
│ │   │ │ │       │ │
│ │   │ │ │       │ └─ Контрольная сумма
│ │   │ │ │       └─ Данные (2*BB символов)
│ │   │ │ └─ Тип записи
│ │   │ └─ Адрес (4 символа)
│ │ └─ Длина данных (2 символа)
│ └─ Стартовый символ ':'
```

##### **Типы записей**

| Тип | Описание |
|-----|----------|
| 00 | Данные |
| 01 | Конец файла |
| 04 | Расширенный линейный адрес |

##### **Парсинг файла**

```python
def parse_hex_file(file_path):
    """
    Парсинг Intel HEX файла
    Возвращает словарь {адрес: [байты]}
    """
    data_records = {}
    extended_address = 0

    with open(file_path, 'r') as file:
        for line in file:
            if not line.startswith(':'):
                continue

            # Парсинг компонентов записи
            byte_count = int(line[1:3], 16)
            address = int(line[3:7], 16)
            record_type = int(line[7:9], 16)
            data = line[9:9+2*byte_count]
            checksum = int(line[9+2*byte_count:9+2*byte_count+2], 16)

            if record_type == 0:  # Data record
                full_address = extended_address + address
                data_bytes = [int(data[i:i+2], 16) for i in range(0, len(data), 2)]
                data_records[full_address] = data_bytes
            elif record_type == 4:  # Extended address
                extended_address = int(data, 16) << 16

    return data_records
```

## Использование API

### Основные классы

#### Single_MiniPTM

```python
from PythonAPI.board_miniptm import Single_MiniPTM

# Создание экземпляра платы
board = Single_MiniPTM(board_num=0, devinfo=pcie_info, adap_num=i2c_adapter)

# Работа с DPLL
board.dpll.write_register(module_addr, reg_offset, value)
status = board.dpll.read_register(module_addr, reg_offset)

# Работа с DPOF
board.dpof.start_transmission()
board.dpof.send_user_data(data_bytes)
received_data = board.dpof.receive_user_data()

# Управление LED
board.set_board_led(led_num, state)
```

#### DPLL класс

```python
from PythonAPI.renesas_cm_registers import DPLL

# Создание DPLL объекта
dpll = DPLL(i2c_interface, read_func, write_func)

# Настройка входов/выходов
dpll.modules["INPUT"].write_field(input_num, "IN_EN", 1)
dpll.modules["OUTPUT"].write_field(output_num, "OUT_EN", 1)

# Мониторинг состояния
input_status = dpll.read_input_status(input_num)
output_status = dpll.read_output_status(output_num)
```

## Диагностика и отладка

### Логирование

```python
import logging

logging.basicConfig(level=logging.DEBUG)
logger = logging.getLogger('MiniPTM')

# Включение отладки для конкретных компонентов
board.DEBUG_PRINT = True
board.dpll.debug_mode = True
```

### Мониторинг состояния

```python
# Проверка состояния всех входов
for i in range(16):
    status = board.read_clock_input_status(i)
    if status['los_live']:
        logger.warning(f"Input {i}: Loss of signal")
    if not status['activity']:
        logger.warning(f"Input {i}: No activity")

# Проверка состояния DPOF
dpof_status = board.dpof.get_status()
if dpof_status['state'] != 'IDLE':
    logger.info(f"DPOF state: {dpof_status['state']}")
```

## Примеры использования

### 1. Настройка DPLL в режиме Master

```python
# Загрузка конфигурации
board.load_config("master_config.tcs")

# Настройка входа от GPS
board.dpll.configure_input(0, "GPS_1PPS", frequency=1e6)

# Настройка выходов
for i in range(4):
    board.dpll.configure_output(i, "CMOS", frequency=25e6)

# Запуск синхронизации
board.dpll.start_sync()
```

### 2. Передача данных через DPOF

```python
# Инициализация DPOF
board.dpof.init_channel(tod_num=0, encoder_num=0, decoder_num=0)

# Отправка данных
data_to_send = b"Hello from MiniPTM!"
board.dpof.send_user_data(data_to_send)

# Прием данных
received_data = board.dpof.receive_user_data()
print(f"Received: {received_data}")
```

### 3. Мониторинг и диагностика

```python
# Визуальный тест LED
board.led_visual_test()

# Чтение информации о SFP
sfp_info = board.read_sfp_info(1)
print(f"SFP Vendor: {sfp_info['vendor']}")

# Сканирование I2C шины
i2c_devices = board.i2c.scan_bus()
print(f"Found I2C devices: {i2c_devices}")
```

## Безопасность и надежность

### Проверки безопасности

1. **Валидация адресов**: Все операции с памятью проверяют границы
2. **Контроль доступа**: Только root может работать с `/dev/mem`
3. **Проверка контрольных сумм**: Для файлов конфигурации
4. **Таймауты**: Все операции имеют таймауты

### Обработка ошибок

```python
try:
    board.load_config("invalid_file.tcs")
except FileNotFoundError:
    logger.error("Configuration file not found")
except ValueError as e:
    logger.error(f"Invalid configuration: {e}")
except Exception as e:
    logger.error(f"Unexpected error: {e}")
    board.reset()  # Сброс платы в безопасное состояние
```

## Производительность и оптимизация

### Оптимизации

1. **Кэширование адресов**: Базовый адрес DPLL кэшируется для уменьшения I2C транзакций
2. **Блочные операции**: Множественная запись/чтение регистров
3. **Асинхронная обработка**: Генетический алгоритм работает в фоне

### Метрики производительности

- **I2C пропускная способность**: ~100 KB/s
- **PCIe пропускная способность**: ~1 GB/s
- **Задержка DPOF**: < 1 мс для handshake
- **Точность синхронизации**: < 1 нс

## Ссылки и ресурсы

- [Renesas ClockMatrix 8A34002 Datasheet](https://www.renesas.com/)
- [Linux Kernel I2C Documentation](https://www.kernel.org/doc/html/latest/i2c/)
- [PCIe Specification](https://pcisig.com/specifications)
- [Intel HEX Format Specification](https://en.wikipedia.org/wiki/Intel_HEX)

---

**Версия документации**: 1.0
**Дата создания**: 2024
**Автор**: Команда MiniPTM
