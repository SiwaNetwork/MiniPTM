
# Импорт необходимых модулей
from renesas_cm_configfiles import *
from smbus2 import SMBus
import time

# Параметры программирования EEPROM
eeprom_file="MiniPTMV4_BaseConfig_4-10-2024.hex"  # Файл с данными для записи
i2c_bus=4                # Номер I2C шины
eeprom_addr=0x54         # Адрес EEPROM на шине I2C
block_select_bit=1       # Бит выбора блока памяти
write_delay=0.005        # Задержка 5мс после записи



def write_eeprom(bus, device_address, start_memory_address, data_list):
    """
    Функция записи данных в EEPROM
    bus - номер I2C шины
    device_address - адрес устройства EEPROM
    start_memory_address - начальный адрес памяти для записи
    data_list - список байтов для записи
    """
    with SMBus(bus) as smbus:
        for offset, data in enumerate(data_list):
            # Вычисляем полный адрес памяти
            memory_address = start_memory_address + offset
            # Формируем адрес устройства с учетом блока памяти
            block_device_address = device_address | ((memory_address >> 16) & block_select_bit)
            memory_address_low = memory_address & 0xFFFF
            # Подготавливаем байты адреса
            address_bytes = [memory_address_low >> 8, memory_address_low & 0xFF]
            # Записываем данные используя write_i2c_block_data
            #print(f"Block data addr=0x{block_device_address:02x}, addr0=0x{address_bytes[0]:02x}")
            smbus.write_i2c_block_data(block_device_address, address_bytes[0], address_bytes[1:] + [data])
            time.sleep(write_delay)  # Задержка после записи каждого байта


# Основная программа
# Парсим Intel HEX файл с данными для EEPROM
hex_file_data, non_data_records_debug = parse_intel_hex_file( eeprom_file)
# Записываем данные по адресам
for addr in hex_file_data.keys():
    print(f"Write addr 0x{addr:02x} = {hex_file_data[addr]}")
    write_eeprom(i2c_bus, eeprom_addr, addr, hex_file_data[addr])

