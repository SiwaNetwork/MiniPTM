
# Импорт модуля для работы с I2C
from i2c_miniptm import miniptm_i2c


from enum import Enum
#########
# Общая архитектура модуля:
# 1. Он записывает конфигурацию напрямую в чипсет

# Перечисление режимов работы GPIO
class gpiomode(Enum):
    INPUT = 0      # Режим входа
    OUTPUT = 1     # Режим выхода
    FUNCTION = 2   # Специальная функция
    
    
# Класс для управления GPIO пинами Renesas CM
class cm_gpios:
    def __init__(self, i2c_dev):
        """
        Инициализация GPIO контроллера
        i2c_dev - устройство I2C для коммуникации
        """
        self.i2c_dev = i2c_dev
        # Базовые адреса для каждого GPIO пина (0-15)
        self.base_addrs = [0xc8c2, 0xc8d4, 0xc8e6, 0xc900, 0xc912,
                0xc924, 0xc936, 0xc948, 0xc95a, 0xc980, 0xc992, 
                0xc9a4, 0xc9b6, 0xc9c8, 0xc9da, 0xca00]
        # Допустимые номера пинов
        self.valid_num = [i for i in range(0,16)]

    # Режим может иметь много значений в теории
    # Кодирование: 1 = выход, 0 = вход
    def configure_pin(self, pin_num: int, mode: int, value: bool):
        """
        Конфигурация GPIO пина
        pin_num - номер пина (0-15)
        mode - режим работы (INPUT/OUTPUT/FUNCTION)
        value - значение для установки (True/False)
        """
        if pin_num not in self.valid_num:
            return
        #print(f"Configure DPLL GPIO{pin_num} mode {mode} value {value}")
        # Записываем функцию включения GPIO и режим CMOS
        if ( mode == gpiomode.INPUT ):
            # 0x10 в руководстве по программированию v4.9, 0x11 в 5.3, предполагаем 4.9
            self.i2c_dev.write_dpll_reg(self.base_addrs[pin_num], 0x10, 0x0) # Триггерный регистр
        elif ( mode == gpiomode.OUTPUT ):            
            # 0x2 в 5.3, 0x0 в v4.9, предполагаем 4.9
            if ( pin_num >= 8 ):
                # Для пинов 8-15 используем регистр 0xc161
                read_val = self.i2c_dev.read_dpll_reg(0xc161, 0x0)
                if ( value ):                
                    read_val |= (1 << pin_num-8)  # Устанавливаем бит
                else:
                    read_val &= ~(1 << pin_num-8)  # Сбрасываем бит
                self.i2c_dev.write_dpll_reg(0xc161, 0x0, read_val)
                self.i2c_dev.write_dpll_reg(0xc161, 0x0, self.i2c_dev.read_dpll_reg(0xc161, 0x0) ) # Триггерный регистр
                self.i2c_dev.write_dpll_reg(self.base_addrs[pin_num], 0x10, 0x4) # Триггерный регистр GPIO и установка на выход
            else:
                # Для пинов 0-7 используем регистр 0xc160
                read_val = self.i2c_dev.read_dpll_reg(0xc160, 0x0)
                if ( value ):                
                    read_val |= (1 << pin_num)
                else:
                    read_val &= ~(1 << pin_num)
                self.i2c_dev.write_dpll_reg(0xc160, 0x0, read_val)
                self.i2c_dev.write_dpll_reg(0xc160, 0x1, self.i2c_dev.read_dpll_reg(0xc160, 0x1) ) # Триггерный регистр
                self.i2c_dev.write_dpll_reg(self.base_addrs[pin_num], 0x10, 0x4) # Триггерный регистр GPIO и установка на выход

    # Возвращает [режим, значение]
    def read_pin_mode(self, pin_num: int) -> [int, int]:
        """
        Чтение режима и значения GPIO пина
        pin_num - номер пина
        Возвращает список [режим, значение]
        """
        if pin_num not in self.valid_num:
            return 
        mode = self.i2c_dev.read_dpll_reg(self.base_addrs[pin_num], 0x10)
        # Декодирование режима из битов регистра
        if ( mode & 0x4 ) and ~( mode & 0x1 ):
            mode = gpiomode.OUTPUT
        elif ~(mode & 0x4) and ~(mode & 0x1):
            mode = gpiomode.INPUT
        elif (mode & 0x1):
            mode = gpiomode.FUNCTION
            
        val = self.i2c_dev.read_dpll_reg(0xc03c, 0x8a) # Регистр уровня GPIO
        val = (val >> pin_num) & 0x1  # Извлекаем бит для нужного пина
        return [ mode, val ] 
        
    def print_status(self, pin_num: int):
        """
        Вывод статуса GPIO пина
        pin_num - номер пина
        """
        if pin_num not in self.valid_num:
            return
        [mode,val] = self.read_pin_mode(pin_num)
        mode_str = ""
        if ( mode == gpiomode.INPUT ):
            mode_str = "Input"
        elif ( mode == gpiomode.OUTPUT):
            mode_str = "Output"
        elif ( mode == gpiomode.FUNCTION):
            mode_str = "Function"
        else:
            mode_str = "UNKNOWN"
        print(f"GPIO{pin_num} mode={mode_str} value={val}")
        
    def __str__(self):
        """
        Строковое представление - выводит статус всех GPIO
        """
        # Читаем обратно все конфигурации
        for i in self.valid_num:
            self.print_status(i)
            
        
        
 
