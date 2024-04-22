# Flipper-SX1262-LoRa

# UNDER CONSTRUCTION

# Welcome to the flipper-SX1262-LoRa wiki!

This plug-in includes functionalities for working with LoRa radio communication signals. Now you can interact with LoRa transmissions using the Flipper Zero pocket device, basic tasks such as sniffing and injection are available, making it easy to perform activities such as analysis, error detection and configuration of new peripherals to the network.

# Requirements
* Electronic Cats Flipper Addon "subGHz-LoRa"
![image](https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/addonsubghz.png)
* Unleashed Firmware Installed on Flipper (Tested on unlshd-071e)

# Menus description

## Follow the screens to find the main menu
<img width="256" alt="Main2" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/Apps.png">
<img width="256" alt="Main2" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/misc.png">
<img width="256" alt="Main2" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/lora_relay.png">
<img width="256" alt="Main1" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/lora_settings.png">

Select what you wanna do by clicking one of the options on the screen
* LoRa settings (Change main LoRa parameteres)
* LoRa sniffer (Watch data traveling trought the specific LoRa settings)
* LoRa sender (Send a file containing LoRa messages to any peripheral listening on the network.)

<!---  About (See general information of the plugin) --->

## Settings Menu
<img width="256" alt="LoRa Settings" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/settings.png">

* Frequency (in MHz)
* Bandwidth (LoRa communication allowed values)
* Spread Factor (the speed at which the signal frequency changes across the bandwidth of a channel)
<!--- Data Rate (Flag indicating if the set bits number is odd or even, none also is a valid value) --->

## Sniffer Menu

<img width="256" alt="UartSettings" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/sniffer.png">

* Use right key in D-pad to start sniffing
<img width="256" alt="UartSettings" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/start_sniff.png">

* The first 8 bytes of the LoRa messages received will be displayed according to the established parameters and their ASCII representation if available
<img width="256" alt="UartSettings" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/receive.png">

* Use central key in D-pad to start/stop recording LoRa messages to log file

<img width="256" alt="UartSettings" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/writelog.png">


* A succesfull comunication between flipper and another LoRa device will depend on LoRa parameters configured, you must know how are they configured in the target LoRa network, wrong configurations will result in a data loss

## Sender Menu
* Send data log file

<img width="256" alt="Sender Menu" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/lora_sender.png">

<!--- Manual Sender (Build a packet manually and send it) --->
<!--- Buffer Sender (A list with the most recent master sniffed requests, open any of them, modify it and send it) --->

* Use central key in D-pad to start Browser

<img width="256" alt="Opening LOG file" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/select_file.png">

* Browse in your files, look for a log file and send it.

<img width="256" alt="Opening LOG file" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/data.png">
<img width="256" alt="Opening LOG file" src="https://github.com/ElectronicCats/flipper-SX1262-LoRa/blob/main/assets/lora_tx.png">

<!--- ## About --->
<!--- Shows general information about this cool plugin --->
 


