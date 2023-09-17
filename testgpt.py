# function to read all mcp2317 registers
# 2018-05-05
# Version 1.0

import smbus
import time

bus = smbus.SMBus(1)
address = 0x21

def read_all_registry():
    for i in range(0, 22):
        print("Register: ", i, " Value: ", bus.read_byte_data(address, i))
        time.sleep(0.1)
        
read_all_registry()

