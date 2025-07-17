# Объектный файл модуля ядра
obj-m += miniptm_module.o

# Цель по умолчанию - сборка модуля
all:
	# Компиляция модуля с использованием системы сборки ядра Linux
	# -C указывает путь к исходникам ядра
	# M=$(PWD) указывает путь к исходникам модуля
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules

# Очистка скомпилированных файлов
clean:
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean


# Полный тест: очистка, сборка, загрузка и проверка модуля
full_test:
	# Очищаем старые файлы
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) clean
	# Компилируем модуль
	make -C /lib/modules/$(shell uname -r)/build M=$(PWD) modules
	# Загружаем модуль в ядро
	insmod miniptm_module.ko
	# Показываем последние 10 строк системного лога
	dmesg | tail -n 10
