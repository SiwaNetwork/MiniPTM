import mmap      # Для работы с отображением памяти
import os
import struct    # Для упаковки/распаковки двоичных данных
import subprocess # Для запуска системных команд
import re        # Регулярные выражения
from enum import Enum
import time
from ctypes import *

# Универсальный класс для работы с PCIe устройством


class PCIeDevice:
    """
    Класс для работы с PCIe устройством через отображение памяти BAR
    """
    def __init__(self, bar_address_hex, bar_size, access=mmap.ACCESS_WRITE):
        """
        Инициализация PCIe устройства
        bar_address_hex - адрес BAR в шестнадцатеричном формате
        bar_size - размер BAR (можно указать с суффиксом K/M/G)
        access - режим доступа к памяти
        """
        self.bar_address = int(bar_address_hex, 16)
        self.bar_size = self.convert_size_to_bytes(bar_size)
        self.access = access
        self.mm = None
        self._map_memory()

    def convert_size_to_bytes(self, size_str):
        """
        Преобразование размера из строки с суффиксом в байты
        Поддерживает суффиксы K (килобайты), M (мегабайты), G (гигабайты)
        """
        size_units = {"K": 1024, "M": 1024**2, "G": 1024**3}
        match = re.match(r"(\d+)([KMG])", size_str)
        if match:
            size, unit = match.groups()
            return int(size) * size_units[unit]
        return int(size_str)  # Возвращаем как есть, если нет суффикса

    def _map_memory(self):
        """
        Отображение памяти устройства в адресное пространство процесса
        Использует /dev/mem для доступа к физической памяти
        """
        try:
            with open("/dev/mem", "r+b") as f:
                self.mm = mmap.mmap(f.fileno(), self.bar_size,
                                    offset=self.bar_address, access=self.access)
        except IOError as e:
            print(f"Error mapping memory: {e}")
            self.mm = None

    def read32(self, offset):
        """
        Чтение 32-битного значения по смещению
        offset - смещение от начала BAR
        """
        if self.mm is None:
            raise Exception("Memory not mapped")
        if offset + 4 > self.bar_size:
            raise ValueError("Read exceeds BAR size")
        self.mm.seek(offset)
        data = self.mm.read(4)
        #print(f"PCIe read32 0x{offset:x}=0x{struct.unpack('I', data)[0]:x}")
        return struct.unpack('I', data)[0]  # Распаковка как unsigned int

    def write32(self, offset, value):
        """
        Запись 32-битного значения по смещению
        offset - смещение от начала BAR
        value - записываемое значение
        """
        #print(f"PCIe write32 0x{offset:x}=0x{value:x}")
        if self.mm is None:
            raise Exception("Memory not mapped")
        if offset + 4 > self.bar_size:
            raise ValueError("Write exceeds BAR size")
        self.mm.seek(offset)
        self.mm.write(struct.pack('I', value))

    def close(self):
        if self.mm:
            self.mm.close()
            self.mm = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()


def get_ethernet_devices():
    try:
        output = subprocess.check_output(["lspci", "-nn"]).decode()
        ethernet_devices = re.findall(
            r'(\w\w:\w\w.\w) Ethernet controller.*\[(\w+):(\w+)\]', output)
        return ethernet_devices
    except subprocess.CalledProcessError as e:
        print("An error occurred while running lspci:", e)
        return []


def get_mac_address(pci_address):
    interface_path = f"/sys/bus/pci/devices/0000:{pci_address}/net/"
    try:
        interface_name = os.listdir(interface_path)[0]
    except IndexError:
        return None

    try:
        with open(f"/sys/class/net/{interface_name}/address") as f:
            return f.read().strip()
    except IOError:
        return None


def get_bar_address_and_size(pci_id):
    try:
        # Run the lspci command with the specific PCI ID
        output = subprocess.check_output(
            ["lspci", "-s", pci_id, "-vvv"], universal_newlines=True)

        # Extract the Memory Regions (BAR) information
        bar_info = re.findall(
            r'Region \d: Memory at (\w+) .* \[size=(\w+)\]', output)

        return bar_info
    except subprocess.CalledProcessError as e:
        print(f"An error occurred while running lspci for device {pci_id}:", e)
        return None


# returns [ [miniptm0 device like 01:00.0, bar address 0, bar size 0] [][] ]
def get_miniptm_devices():
    # Define your specific values here
    specific_vendor_id = "8086"
    specific_device_id = "125b"
    specific_mac_address = "00:a0:c9:00:00:00"

    miniptm_pcie_devices = []
    # Get Ethernet devices
    ethernet_devices = get_ethernet_devices()

    # Process each device
    for pci_address, vendor_id, device_id in ethernet_devices:
        mac_address = get_mac_address(pci_address)

        # Check if the device matches specific criteria
        if vendor_id == specific_vendor_id and device_id == specific_device_id and mac_address == specific_mac_address:
            bar_info = get_bar_address_and_size(pci_address)
            miniptm_pcie_devices.append(
                [pci_address, bar_info[0][0], bar_info[0][1]])
            if bar_info:
                # for i, (address, size) in enumerate(bar_info):
                #    print(f"Region {i}: Address = {address}, Size = {size}")
                # print(f"Match found: PCI Address: {pci_address}, Vendor ID: {vendor_id}, Device ID: {device_id}, MAC Address: {mac_address}, BAR0 Address: {bar_info[0][0]} BAR0 Size: {bar_info[0][1]}")
                pass
            else:
                # print(f"BAR information not found for PCI Address: {pci_address}")
                pass
        else:
            # print(f"No match: PCI Address: {pci_address}, Vendor ID: {vendor_id}, Device ID: {device_id}, MAC Address: {mac_address}")
            pass

    return miniptm_pcie_devices


####################################################################################
# Ok now enumerated MiniPTM boards and have all the info to access them, write access class
# enable direct DMA access to the I225, may be useful in the future
# i2c is handled through kernel module


class MiniPTM_PCIe(PCIeDevice):
    def __init__(self, bar_address_hex, bar_size):
        super().__init__(bar_address_hex, bar_size)

    def close(self):
        if self.mm:
            self.clib.unmap_memory(self.mm, self.mapped_size)
            self.mm = None

    def __enter__(self):
        return self

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.close()






if __name__ == "__main__":

    miniptm_devs = get_miniptm_devices()
    print(f"Got {len(miniptm_devs)} mini ptm devices: {miniptm_devs}")

    for [pci_dev, bar, bar_size] in miniptm_devs:
        # Create an instance of PCIeDevice
        driver = MiniPTM_PCIe(bar, bar_size)
