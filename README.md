# Flipper-SX1262-LoRa

# UNDER CONSTRUCTION

# Welcome to the flipper-SX1262-LoRa wiki!

This plug-in includes functionalities for working with LoRa radio communication signals. Now you can interact with LoRa transmissions using the Flipper Zero pocket device, basic tasks such as sniffing and injection are available, making it easy to perform activities such as analysis, error detection and configuration of new peripherals to the network.

# Requirements
* Electronic Cats Flipper Addon "subGHz-LoRa"
* Unleashed Firmware Installed on Flipper (Tested on unlshd-071e)
![image](https://github.com/ElectronicCats/flipper-rs485modbus/assets/lora_relay.png)


# Menus description
## Main Menu
<img width="256" alt="Main1" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/30b6ccfe-8456-4de0-ac1f-4c5922c8592e">
<img width="256" alt="Main2" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/2e767849-24cd-4e28-ab94-a070dec21bf5">

Select what you wanna do by clicking one of the options on the screen
* Settings (Change general configurations)
* Sniffer (Watch data traveling trought the Bus)
* Sender (Send a packet request to any peripheral on the network)
* Read LOG (Open and read a previous sniffing sesion stored in the SD)
* About (See general information of the plugin)

## Settings Menu
<img width="256" alt="UartSettings" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/c0b1108f-b3bb-466a-a5c4-ea36e636ed95">
<img width="256" alt="AdditionalSettings" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/fa343080-9602-4fcb-a989-acc53fc6c2d0">

* Baudrate (bits transmitted per second)
* Data size (Character size in bits)
* Stop bits (Number of bits indicating the end of the character)
* Parity (Flag indicating if the set bits number is odd or even, none also is a valid value)
* TimeOut (Maximum amount of time to wait a response packet)
* OutputFormat ("Hex" format displays hexadecimal values and "Default" in a conventional way)
* SaveLOG? (Stores all sniffed data in a LOG file, if it's enabled)

A succesfull comunication between flipper and Modbus will depend on these parameters, you must know how are they configured in the target Modbus network, wrong configurations will result in a data loss.

## Sender Menu
<img width="256" alt="SenderMenu" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/ca44eb5e-add8-4abd-9970-17772c132de6">

* Manual Sender (Build a packet manually and send it)
* Buffer Sender (A list with the most recent master sniffed requests, open any of them, modify it and send it)

## Read LOG
<img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/9f7339f1-a4aa-408e-ac58-932c12651208">

Browse in your files, look for a LOG file and read it.

## About
Shows general information about this cool plugin 

# How to build a packet manually and send it
### 1. Click on Sender

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/f8b69595-5b33-4980-8e41-46d36355e9ae">

### 2. Open the Manual Sender

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/245ed831-cce7-442d-87b5-efd6474447b1">

### 3. Build the packet 
* Peripheral ID: Change the peripheral target by clicking the right or left button, also you can click the center button and enter the value in the Hexadecimal format, maximum value is 32 or 0x20.

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/a8f5a0d8-7f78-4aeb-aa9c-92459f78eea4"> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/fc84d7cd-a9a4-4f5d-a9ba-d8dea298a6b9">

* Function: Hexadecimal input is disbaled in this field. Supported values are:

   * Read Coils (0x01)
   * Read Discrete Inputs (0x02)
   * Read Holding Registers (0x03)
   * Read Input Registers (0x04)
   * Write Single Coil (0x05)
   * Write Single Register(0x06)
   * Write Multiple Coils (0x0F)
   * Write Multiple Registers (0x10)
* Start address: Choose the start address of coils or registers that you want to read or write. Hexadecimal input available in this field.

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/8e64f312-2466-4ad9-b410-791414d4c8fe">

* Additional fields: The following fields of the packet are variable depending on the selected function, fields like byte count, quantity, value, byte and register may appear. Byte count is the unique field that can not be modified, the other fields can be modified using the buttons or the hexadecimal input.

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/f27f83b1-9701-4b8c-99be-2b317de43bf6"> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/f049fbf3-7692-4731-8a06-e92c03708955"> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/48b220f6-0220-41cc-92f1-8e27b88d81da"> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/9fa8b988-cb76-4c30-82cf-387ada65103f"> 

   See the oficial Modbus documentation to learn more about the fields of a request packet in https://modbus.org/docs/Modbus_Application_Protocol_V1_1b.pdf, special attention on _**Function codes descriptions**_

### 4. Send the packet
Once the packet is complete, send it by clicking the "Send packet" button, this action change automatically the scene to the output console where you can see the peripheral response.

> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/af46f901-9f07-422b-9a81-19c3ec149ae2"> <img width="256" alt="OpeningLOG" src="https://github.com/ElectronicCats/flipper-rs485modbus/assets/156373864/de84ca84-282d-4d26-85dc-0e430c2f8bb0">

In the right picture you can see the response, this response includes function, peripheral ID, byte count and the 4 values requested by the hub. See https://modbus.org/docs/Modbus_Application_Protocol_V1_1b.pdf to learn more about response structures.
