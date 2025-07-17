#include <linux/init.h>
#include <linux/module.h>
#include <linux/io.h>
#include <linux/pci.h>
#include <linux/gpio.h>
#include <linux/gpio/driver.h>
#include <linux/i2c.h>
#include <linux/i2c-algo-bit.h>
#include <linux/list.h>
#include <linux/etherdevice.h>

#define VENDOR_ID 0x8086 // ID производителя PCIe устройства (Intel)
#define DEVICE_ID 0x125b // ID устройства PCIe


/*********** Определения для работы с SDP (Software Definable Pins) ********/

// Регистры I225 начинаются со страницы 361 в руководстве пользователя (PDF)


// Регистр CTRL и связанные биты для управления пинами SDP0 / SDP1
#define CTRL 0x0

#define DIR_IN 0   // Направление - вход
#define DIR_OUT 1  // Направление - выход

#define SDP0_IODIR (1<<22)  // Бит направления для SDP0
#define SDP1_IODIR (1<<23)  // Бит направления для SDP1
#define SDP0_DATA (1<<2)    // Бит данных для SDP0
#define SDP1_DATA (1<<3)    // Бит данных для SDP1

// Регистр CTRL_EXT и связанные биты для управления SDP2 / SDP3
#define CTRL_EXT 0x18

#define SDP2_IODIR (1<<10)  // Бит направления для SDP2
#define SDP3_IODIR (1<<11)  // Бит направления для SDP3
#define SDP2_DATA (1<<6)    // Бит данных для SDP2
#define SDP3_DATA (1<<7)    // Бит данных для SDP3

// Регистр конфигурации LED1
#define LED_CONFIG 0xe00
#define LED_ALWAYS_ON (0x0)   // LED всегда включен
#define LED_ALWAYS_OFF (0x1)  // LED всегда выключен





MODULE_LICENSE("GPL");
MODULE_AUTHOR("Julian St. James");
MODULE_DESCRIPTION("MiniPTM Kernel Module for SDP2/3");


// Регистрация параметра модуля
//module_param(miniptm_device, charp, S_IRUGO); // charp: указатель на символ, S_IRUGO: права только для чтения


//static void __iomem *mapped_address;
// Структура для хранения информации о GPIO чипе
struct my_gpio_chip {
	struct gpio_chip chip;
	struct device_list * my_dev;
};

// Глобальный список для отслеживания нескольких устройств
struct device_list {
    struct list_head list;           // Для связанного списка
    struct pci_dev *pdev;           // Указатель на PCI устройство
    void __iomem *mapped_address;   // Отображенный адрес в памяти
    struct gpio_chip gpio_chip;     // GPIO чип
    struct i2c_algo_bit_data i2c_bit_data;  // Данные для bit-banging I2C
    struct i2c_adapter i2c_adapter; // Адаптер I2C
    // Другие данные, специфичные для устройства...
};
static LIST_HEAD(device_list_head);  // Голова списка устройств


// Известные MAC-адреса для фильтрации
static const unsigned char known_mac_addresses[][ETH_ALEN] = {
    {0x00, 0x00, 0x00, 0x00, 0x00, 0x01}, 
    {0x00, 0xa0, 0xc9, 0x00, 0x00, 0x00}, 
    // Добавьте больше MAC-адресов здесь...
};




// Настройка GPIO пина как вход
static int my_gpio_direction_input(struct gpio_chip *chip, unsigned offset)
{
    u32 data;
    struct device_list *dev_list;
    void __iomem *mapped_address;

    dev_list = container_of(chip, struct device_list, gpio_chip);
    mapped_address = dev_list->mapped_address;
    //pr_info("GPIO direction_input called. Offset: %u SDP%u\n", offset, offset+2);
    // Реализация настройки GPIO как вход
    // ...

    // Читаем текущее значение регистра CTRL_EXT
    data = ioread32(mapped_address+CTRL_EXT);
    
    //pr_info("GPIO direction_input called. Offset: %u SDP%u init=0x%x\n", offset, offset+2, data);
    // Очищаем бит направления для установки режима входа
    if ( offset == 0 ) {
	data &= ~SDP2_IODIR;  // SDP2 как вход
    } else if ( offset == 1 ) {
	data &= ~SDP3_IODIR;  // SDP3 как вход
    }
    // Записываем обновленное значение обратно
    iowrite32(data, mapped_address+CTRL_EXT);

    return 0;
}

// Настройка GPIO пина как выход
static int my_gpio_direction_output(struct gpio_chip *chip, unsigned offset, int value)
{
    u32 data;
    struct device_list *dev_list;
    void __iomem *mapped_address;

    dev_list = container_of(chip, struct device_list, gpio_chip);
    mapped_address = dev_list->mapped_address;
    // Реализация настройки GPIO как выход
    // ...
    data = ioread32(mapped_address+CTRL_EXT);
    //pr_info("GPIO direction_output called. Offset: %u, Value: %d SDP%u init=0x%x\n", offset, value, offset+2, data);

    // Устанавливаем направление как выход
    if ( offset == 0 ) {
	data |= SDP2_IODIR;  // SDP2 как выход
    } else if ( offset == 1 ) {
	data |= SDP3_IODIR;  // SDP3 как выход
    }
    // Устанавливаем начальное значение
    if ( value ) {
	    // Установить высокий уровень
	    if ( offset == 0 ) {
		data |= SDP2_DATA;
	    } else if ( offset == 1 ) {
		data |= SDP3_DATA;
	    }
    } else {
	    // Установить низкий уровень
	    if ( offset == 0 ) {
		data &= ~SDP2_DATA;
	    } else if ( offset == 1 ) {
		data &= ~SDP3_DATA;
	    }
    }
    iowrite32(data, mapped_address+CTRL_EXT);
    return 0;
}

// Чтение значения GPIO пина
// Возвращает: 0 для низкого уровня, 1 для высокого, отрицательное значение при ошибке
static int my_gpio_get_value(struct gpio_chip *chip, unsigned offset)
{
    u32 data;
    struct device_list *dev_list;
    void __iomem *mapped_address;

    dev_list = container_of(chip, struct device_list, gpio_chip);
    mapped_address = dev_list->mapped_address;
    // Реализация чтения GPIO
    // ...
    data = ioread32(mapped_address+CTRL_EXT);
    //pr_info("GPIO get_value called. Offset: %u SDP%u init=0x%x\n", offset, offset+2, data);
    // Маскируем нужный бит данных
    if ( offset == 0 ) {
	data &= SDP2_DATA;
    } else if ( offset == 1 ) {
	data &= SDP3_DATA;
    }
    // Возвращаем 1 если бит установлен, иначе 0
    if ( data ) {
	return 1;
    }

    return 0;
}

// Установка выходного значения GPIO пина
static void my_gpio_set_value(struct gpio_chip *chip, unsigned offset, int value)
{
    u32 data;
    struct device_list *dev_list;
    void __iomem *mapped_address;

    dev_list = container_of(chip, struct device_list, gpio_chip);
    mapped_address = dev_list->mapped_address;
    // Реализация записи в GPIO
    // ...
    data = ioread32(mapped_address+CTRL_EXT);
    //pr_info("GPIO set_value called. Offset: %u, Value: %d SDP%u init=0x%x\n", offset, value, offset+2, data);
    if ( value ) {
	    // Установить высокий уровень
	    if ( offset == 0 ) {
		data |= SDP2_DATA;
	    } else if ( offset == 1 ) {
		data |= SDP3_DATA;
	    }
    } else {
	    // Установить низкий уровень
	    if ( offset == 0 ) {
		data &= ~SDP2_DATA;
	    } else if ( offset == 1 ) {
		data &= ~SDP3_DATA;
	    }
    }
    iowrite32(data, mapped_address+CTRL_EXT);
}









// Функции для реализации I2C через GPIO (bit-banging)
// SDP2 (0) = SDA (линия данных), SDP3 (1) = SCL (линия тактирования)
/*
** Функция для чтения линии SCL (тактирования)
*/
static int MiniPTM_Read_SCL(void *data)
{
    struct device_list *dev_list;
    dev_list = data;

  my_gpio_direction_input(&dev_list->gpio_chip, 1);  // SCL как вход
  return my_gpio_get_value(&dev_list->gpio_chip, 1);  // Читаем значение SCL
}
/*
** Функция для чтения линии SDA (данных)
*/
static int MiniPTM_Read_SDA(void *data)
{
    struct device_list *dev_list;
    dev_list = data;
  my_gpio_direction_input(&dev_list->gpio_chip, 0);  // SDA как вход
  return my_gpio_get_value(&dev_list->gpio_chip, 0);  // Читаем значение SDA
}
/*
** Функция для установки линии SCL (тактирования)
*/
static void MiniPTM_Set_SCL(void *data, int state)
{
    struct device_list *dev_list;
    dev_list = data;
  if ( state ) { // Открытый сток - устанавливаем как вход для высокого уровня
	  my_gpio_direction_input(&dev_list->gpio_chip, 1);
  } else {
	  my_gpio_direction_output(&dev_list->gpio_chip, 1, 0);
  }
}
/*
** Функция для установки линии SDA (данных)
*/
static void MiniPTM_Set_SDA(void *data, int state)
{
    struct device_list *dev_list;
    dev_list = data;
  if ( state ) { // Открытый сток - устанавливаем как вход для высокого уровня
	  my_gpio_direction_input(&dev_list->gpio_chip, 0);
  } else {
	  my_gpio_direction_output(&dev_list->gpio_chip, 0, 0);
  }
}



static bool is_mac_address_known(const unsigned char *mac_addr) {
    """
    Проверка, является ли MAC-адрес известным
    mac_addr - проверяемый MAC-адрес
    Возвращает true если адрес найден в списке известных
    """
    char device_mac[ETH_ALEN * 3]; // 2 символа на байт + разделитель ':'
    char known_mac[ETH_ALEN * 3];
    int i;

    // Форматируем MAC-адрес устройства для вывода
    snprintf(device_mac, sizeof(device_mac), "%pM", mac_addr);

    // Проверяем каждый известный MAC-адрес
    for (i = 0; i < ARRAY_SIZE(known_mac_addresses); ++i) {
        snprintf(known_mac, sizeof(known_mac), "%pM", known_mac_addresses[i]);
        pr_info("Comparing device MAC: %s with known MAC: %s\n", device_mac, known_mac);

        // Сравниваем MAC-адреса побайтово
        if (memcmp(mac_addr, known_mac_addresses[i], ETH_ALEN) == 0) {
            pr_info("MAC address match found\n");
            return true;
        }
    }
    pr_info("No MAC address match found\n");
    return false;
}



static int __init miniptm_module_init(void)
{
    """
    Функция инициализации модуля ядра
    Вызывается при загрузке модуля
    """
    //u32 data;
    int ret;
    resource_size_t my_bar;
    resource_size_t my_len;


    struct pci_dev *pdev = NULL;
    struct device_list * dev_list;

    pr_info("Julian's MiniPTM Module start device \n");

    pdev = 0;
    my_bar = 0;
    my_len = 0;


    // Перебираем все PCIe устройства с указанными vendor и device ID
    while ((pdev = pci_get_device(VENDOR_ID, DEVICE_ID, pdev)) != NULL) {
	bool mac_match;
	// Проверяем, является ли устройство сетевым
	struct net_device *netdev;



	mac_match = false;
	netdev = pci_get_drvdata(pdev);

	if (!netdev || !is_valid_ether_addr(netdev->dev_addr)) {
		pr_info("Device not ethernet!\n");
		continue; // Не сетевое устройство или неверный MAC-адрес
	}


	// Проверяем, совпадает ли MAC-адрес с одним из известных адресов
	if (netdev && is_valid_ether_addr(netdev->dev_addr)) {
		if ( is_mac_address_known(netdev->dev_addr) ) {
			mac_match = true;
		}
	}

	if (!mac_match) {
		pr_info("MAC doesn't match!\n");
		continue; // MAC-адрес не совпадает
	}

	// Устройство соответствует критериям; выделяем и инициализируем элемент device_list
	dev_list = kzalloc(sizeof(*dev_list), GFP_KERNEL);
	if (!dev_list) {
		// Обработка ошибки выделения памяти...
	}

	dev_list->pdev = pdev;


	// map BAR 0
	my_bar = pci_resource_start(pdev, 0);
	my_len = pci_resource_len(pdev, 0);
	pr_info("  BAR 0: Start Address: 0x%llx, Length: 0x%llx\n",
		(unsigned long long)my_bar, (unsigned long long)my_len);


	dev_list->mapped_address = ioremap(my_bar, my_len);

	/*
	// Read data from the fixed address
	data = ioread32(dev_list->mapped_address);
	pr_info("Read data from fixed address: 0x%x\n", data);
	*/

	dev_list->gpio_chip.label = "MiniPTM_GPIO";
	dev_list->gpio_chip.direction_input = my_gpio_direction_input;
	dev_list->gpio_chip.direction_output = my_gpio_direction_output;
	dev_list->gpio_chip.get = my_gpio_get_value;
	dev_list->gpio_chip.set = my_gpio_set_value;
	dev_list->gpio_chip.can_sleep = true;
	dev_list->gpio_chip.base = -1;
	dev_list->gpio_chip.ngpio = 2;

	// Register the GPIO chip with the GPIO subsystem
	ret = gpiochip_add_data(&dev_list->gpio_chip, dev_list);
	if (ret) {
		pr_err("Failed to register GPIO chip: %d\n", ret);
		iounmap(dev_list->mapped_address);
		kfree(dev_list);
		continue;
	}


	// Настройка функций bit-banging I2C
	dev_list->i2c_bit_data.setsda = MiniPTM_Set_SDA;
	dev_list->i2c_bit_data.setscl = MiniPTM_Set_SCL;
	dev_list->i2c_bit_data.getsda = MiniPTM_Read_SDA;
	dev_list->i2c_bit_data.getscl = MiniPTM_Read_SCL;
	dev_list->i2c_bit_data.udelay = 5;         // Задержка между операциями в микросекундах
	dev_list->i2c_bit_data.timeout = 100;      // Таймаут 100мс
	dev_list->i2c_bit_data.data = dev_list;    // Указатель на родительский devlist

	// Настройка I2C адаптера
	dev_list->i2c_adapter.owner = THIS_MODULE;
	dev_list->i2c_adapter.class = I2C_CLASS_HWMON | I2C_CLASS_SPD;

	strncpy(dev_list->i2c_adapter.name , "MiniPTM I2C Adapter", sizeof(dev_list->i2c_adapter.name) - 1);
	// Гарантируем нуль-терминацию строки
	dev_list->i2c_adapter.name[sizeof(dev_list->i2c_adapter.name) - 1] = '\0';

	dev_list->i2c_adapter.algo_data = &dev_list->i2c_bit_data;
	dev_list->i2c_adapter.nr = -1;  // Автоматическое назначение номера шины
	

	// Добавляем I2C шину в систему
	ret = i2c_bit_add_numbered_bus(&dev_list->i2c_adapter);
	if (ret < 0) {
		pr_err("Failed to add numbered i2c bus: %d\n", ret);
		// Удаляем GPIO чип при ошибке
		gpiochip_remove(&dev_list->gpio_chip);
		iounmap(dev_list->mapped_address);
		kfree(dev_list);
		continue;
	}

	// Добавляем в список только после полной инициализации
	INIT_LIST_HEAD(&dev_list->list); 
	list_add(&dev_list->list, &device_list_head);


	// Специфичные изменения для MiniPTM V4
	// 1. Отключаем функции LED для 1G: LED_SPEED_1000# (LED0) / LED_LINK_ACT# (LED2)
	// 	Это обходное решение для аппаратной ошибки, следующая ревизия должна это исправить
	// 2. LED_SPEED_2500# (LED1) используется для сброса платы
	// 	Устанавливаем как выход и устанавливаем высокий уровень
	// 	Зарезервировано для будущего использования в ПО
	iowrite32( (LED_ALWAYS_OFF << 0) +  // LED0 - всегда выключен
			(LED_ALWAYS_OFF << 8) +  // LED1 - всегда выключен
			(LED_ALWAYS_OFF << 16),  // LED2 - всегда выключен
		dev_list->mapped_address + LED_CONFIG ); // Выключаем все LED


	pr_info("Done Insert 1 MiniPTM Basic module\n");
    }
    return 0;
}

static void __exit miniptm_module_exit(void)
{
    """
    Функция выхода модуля ядра
    Вызывается при выгрузке модуля
    """
    struct device_list *dev_list, *tmp;

    // Безопасный обход списка с возможностью удаления элементов
    list_for_each_entry_safe(dev_list, tmp, &device_list_head, list) {
        // Освобождаем ресурсы для каждого устройства


	// Удаляем I2C адаптер
	i2c_del_adapter(&dev_list->i2c_adapter);	
	// Удаляем GPIO чип
	gpiochip_remove(&dev_list->gpio_chip);
	// Освобождаем отображенную память
	iounmap(dev_list->mapped_address);

        list_del(&dev_list->list); // Удаляем из списка
	kfree(dev_list);  // Освобождаем память структуры
    }


    pr_info("MiniPTM Module: Removed\n");
	
}

// Макросы для регистрации функций инициализации и выхода модуля
module_init(miniptm_module_init);
module_exit(miniptm_module_exit);

