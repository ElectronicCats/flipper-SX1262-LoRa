/*
Test
*/

#include <furi.h>
#include <furi_hal.h>

#define TAG "LoRa"

//Presets. These help make radio config easier
#define PRESET_DEFAULT    0
#define PRESET_LONGRANGE  1
#define PRESET_FAST       2

#define REG_LR_SYNCWORD	0x0740
#define RADIO_READ_REGISTER	0x1D

static uint32_t timeout = 1000;
static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_reset = &gpio_ext_pc1;
const GpioPin* const pin_ant_sw = &gpio_usart_tx;
const GpioPin* const pin_busy = &gpio_usart_rx;
const GpioPin* const pin_dio1 = &gpio_ext_pc3;

bool inReceiveMode = false;
uint8_t spiBuff[32];   //Buffer for sending SPI commands to radio

//Config variables (set to PRESET_DEFAULT on init)
uint32_t pllFrequency;
uint8_t bandwidth;
uint8_t codingRate;
uint8_t spreadingFactor;
uint8_t lowDataRateOptimize;
uint32_t transmitTimeout; //Worst-case transmit time depends on some factors

int rssi = 0;
int snr = 0;
int signalRssi = 0;

void abandone() {
    FURI_LOG_E(TAG, "abandon hope all ye who enter here");
}

/*Convert a frequency in hz (such as 915000000) to the respective PLL setting.
* The radio requires that we set the PLL, which controls the multipler on the internal clock to achieve the desired frequency.
* Valid frequencies are 150mhz to 960mhz (150000000 to 960000000)
*
* NOTE: This assumes the radio is using a 32mhz clock, which is standard.  This is independent of the microcontroller clock
* See datasheet section 13.4.1 for this calculation.
* Example: 915mhz (915000000) has a PLL of 959447040
*/
uint32_t frequencyToPLL(long rfFreq) {

  FURI_LOG_E(TAG, "ENTER - frequencyToPLL()");

  /* Datasheet Says:
    *		rfFreq = (pllFreq * xtalFreq) / 2^25
    * Rewrite to solve for pllFreq
    *		pllFreq = (2^25 * rfFreq)/xtalFreq
    *
    *	In our case, xtalFreq is 32mhz
    *	pllFreq = (2^25 * rfFreq) / 32000000
  */
  //Basically, we need to do "return ((1 << 25) * rfFreq) / 32000000L"
  //It's very important to perform this without losing precision or integer overflow.
  //If arduino supported 64-bit varibales (which it doesn't), we could just do this:
  //    uint64_t firstPart = (1 << 25) * (uint64_t)rfFreq;
  //    return (uint32_t)(firstPart / 32000000L);
  //
  //Instead, we need to break this up mathimatically to avoid integer overflow
  //First, we'll simplify the equation by dividing both parts by 2048 (2^11)
  //    ((1 << 25) * rfFreq) / 32000000L      -->      (16384 * rfFreq) / 15625;
  //
  // Now, we'll divide first, then multiply (multiplying first would cause integer overflow)
  // Because we're dividing, we need to keep track of the remainder to avoid losing precision
  uint32_t q = rfFreq / 15625UL;  //Gives us the result (quotient), rounded down to the nearest integer
  uint32_t r = rfFreq % 15625UL;  //Everything that isn't divisible, aka "the part that hasn't been divided yet"

  //Multiply by 16384 to satisfy the equation above
  q *= 16384UL;
  r *= 16384UL; //Don't forget, this part still needs to be divided because it was too small to divide before
  
  return q + (r / 15625UL);  //Finally divide the the remainder part before adding it back in with the quotient
}

//Set the radio frequency.  Just a single SPI call,
//but this is broken out to make it more convenient to change frequency on-the-fly
//You must set this->pllFrequency before calling this

void updateRadioFrequency() {

  FURI_LOG_E(TAG, "ENTER - updateRadioFrequency()");

  // Set PLL frequency (this is a complicated math equation. See datasheet entry for SetRfFrequency)
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  uint8_t spiBuff[5] = {
    0x86,  // Opcode for set RF Frequency
    (uint8_t)((pllFrequency >> 24) & 0xFF), // MSB of pll frequency
    (uint8_t)((pllFrequency >> 16) & 0xFF),
    (uint8_t)((pllFrequency >>  8) & 0xFF),
    (uint8_t)((pllFrequency >>  0) & 0xFF)  // LSB of frequency
  };
  furi_hal_spi_bus_tx(spi, spiBuff, 5, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for the radio to process command
}

/** (Optional) Set the operating frequency of the radio.
* The 1262 radio supports 150-960Mhz.  This library uses a default of 915Mhz.
* MAKE SURE THAT YOU ARE OPERATING IN A FREQUENCY THAT IS ALLOWED IN YOUR COUNTRY!
* For example, 915mhz (915000000 hz) is safe in the US.
*
* Specify the desired frequency in Hz (eg 915MHZ is 915000000).
* Returns TRUE on success, FALSE on invalid frequency
*/
bool configSetFrequency(long frequencyInHz) {

  FURI_LOG_E(TAG, "ENTER - configSetFrequency()");

  //Make sure the specified frequency is in the valid range.
  if (frequencyInHz < 150000000 || frequencyInHz > 960000000) { return false;}

  //Calculate the PLL frequency (See datasheet section 13.4.1 for calculation)
  //PLL frequency controls the radio's clock multipler to achieve the desired frequency
  pllFrequency = frequencyToPLL(frequencyInHz);
  updateRadioFrequency();
  return true;
}

// Set the radio modulation parameters.
// This is things like bandwidth, spreading factor, coding rate, etc.
// This is broken into its own function because this command might get called frequently
void updateModulationParameters() {

  FURI_LOG_E(TAG, "ENTER - updateModulationParameters()");

  // Set modulation parameters
  // Modulation parameters are:
  //   - SpreadingFactor
  //   - Bandwidth
  //   - CodingRate
  //   - LowDataRateOptimize
  // None of these actually matter that much.  You can set them to anything, and data will still show up
  // on a radio frequency monitor.
  // You just MUST call "setModulationParameters", otherwise the radio won't work at all

  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select

  spiBuff[0] = 0x8B;                // Opcode for "SetModulationParameters"
  spiBuff[1] = spreadingFactor;     // ModParam1 = Spreading Factor.  Can be SF5-SF12, written in hex (0x05-0x0C)
  spiBuff[2] = bandwidth;           // ModParam2 = Bandwidth.  See Datasheet 13.4.5.2 for details. 0x00=7.81khz (slowest)
  spiBuff[3] = codingRate;          // ModParam3 = CodingRate.  Semtech recommends CR_4_5 (which is 0x01).  Options are 0x01-0x04, which correspond to coding rate 5-8 respectively
  spiBuff[4] = lowDataRateOptimize; // LowDataRateOptimize.  0x00 = 0ff, 0x01 = On.  Required to be on for SF11 + SF12

  furi_hal_spi_acquire(spi);
  furi_hal_spi_bus_tx(spi, spiBuff, 5, timeout); // Assuming 'timeout' is defined somewhere in the code
  furi_hal_spi_release(spi);
  
  furi_delay_ms(100); // Give time for the radio to process command

  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select

  // Determine transmit timeout based on spreading factor
  switch (spreadingFactor) {
    case 12:
      transmitTimeout = 252000; // Actual tx time 126 seconds
      break;
    case 11:
      transmitTimeout = 160000; // Actual tx time 81 seconds
      break;
    case 10:
      transmitTimeout = 60000; // Actual tx time 36 seconds
      break;
    case 9:
      transmitTimeout = 40000; // Actual tx time 20 seconds
      break;
    case 8:
      transmitTimeout = 20000; // Actual tx time 11 seconds
      break;
    case 7:
      transmitTimeout = 12000; // Actual tx time 6.3 seconds
      break;
    case 6:
      transmitTimeout = 7000; // Actual tx time 3.7s seconds
      break;
    default:  // SF5
      transmitTimeout = 5000; // Actual tx time 2.2 seconds
      break;
  }
}

/**(Optional) Use one of the pre-made radio configurations
* This is ideal for making simple changes to the radio config
* without needing to understand how the underlying settings work
* 
* Argument: pass in one of the following
*     - PRESET_DEFAULT:   Default radio config.
*                         Medium range, medium speed
*     - PRESET_FAST:      Faster speeds, but less reliable at long ranges.
*                         Use when you need fast data transfer and have radios close together
*     - PRESET_LONGRANGE: Most reliable option, but slow. Suitable when you prioritize
*                         reliability over speed, or when transmitting over long distances
*/
bool configSetPreset(int preset) {

  FURI_LOG_E(TAG, "ENTER - configSetPreset()");

  if (preset == PRESET_DEFAULT) {
    bandwidth = 5;            //250khz
    codingRate = 1;           //CR_4_5
    spreadingFactor = 7;      //SF7
    lowDataRateOptimize = 0;  //Don't optimize (used for SF12 only)
    updateModulationParameters();
    return true;
  }

  if (preset == PRESET_LONGRANGE) {
    bandwidth = 4;            //125khz
    codingRate = 1;           //CR_4_5
    spreadingFactor = 12;     //SF12
    lowDataRateOptimize = 1;  //Optimize for low data rate (SF12 only)
    updateModulationParameters();
    return true;
  }

  if (preset == PRESET_FAST) {
    bandwidth = 6;            //500khz
    codingRate = 1;           //CR_4_5
    spreadingFactor = 5;      //SF5
    lowDataRateOptimize = 0;  //Don't optimize (used for SF12 only)
    updateModulationParameters();
    return true;
  }

  //Invalid preset specified
  return false;
}

/*Send the bare-bones required commands needed for radio to run.
* Do not set custom or optional commands here, please keep this section as simplified as possible.
* Essential commands are found by reading the datasheet
*/
void configureRadioEssentials() {

  FURI_LOG_E(TAG, "ENTER - configureRadioEssentials()");

  // Tell DIO2 to control the RF switch so we don't have to do it manually
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x9D;  //Opcode for "SetDIO2AsRfSwitchCtrl"
  spiBuff[1] = 0x01;  //Enable 
  furi_hal_spi_bus_tx(spi, spiBuff, 2, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for the radio to process command

  // Just a single SPI command to set the frequency, but it's broken out
  // into its own function so we can call it on-the-fly when the config changes
  configSetFrequency(915000000); // Set default frequency to 915mhz

  // Set modem to LoRa (described in datasheet section 13.4.2)
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x8A; // Opcode for "SetPacketType"
  spiBuff[1] = 0x01; // Packet Type: 0x00=GFSK, 0x01=LoRa
  furi_hal_spi_bus_tx(spi, spiBuff, 2, timeout);

  furi_hal_spi_release(spi);
  
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command

  // Set Rx Timeout to reset on SyncWord or Header detection
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x9F; // Opcode for "StopTimerOnPreamble"
  spiBuff[1] = 0x00; // Stop timer on: 0x00=SyncWord or header detection, 0x01=preamble detection
  furi_hal_spi_bus_tx(spi, spiBuff, 2, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command

  // Set modulation parameters is just one more SPI command, but since it
  // is often called frequently when changing the radio config, it's broken up into its own function
  configSetPreset(PRESET_DEFAULT); // Sets default modulation parameters

  // Set PA Config
  // See datasheet 13.1.4 for descriptions and optimal settings recommendations
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x95; // Opcode for "SetPaConfig"
  spiBuff[1] = 0x04; // paDutyCycle. See datasheet, set in conjunction with hpMax
  spiBuff[2] = 0x07; // hpMax. Basically Tx power. 0x00-0x07 where 0x07 is max power
  spiBuff[3] = 0x00; // device select: 0x00 = SX1262, 0x01 = SX1261
  spiBuff[4] = 0x01; // paLut (reserved, always set to 1)
  furi_hal_spi_bus_tx(spi, spiBuff, 5, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command

  // Set TX Params
  // See datasheet 13.4.4 for details
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x8E; // Opcode for SetTxParams
  spiBuff[1] = 22; // Power. Can be -17(0xEF) to +14x0E in Low Pow mode. -9(0xF7) to 22(0x16) in high power mode
  spiBuff[2] = 0x02; // Ramp time. Lookup table. See table 13-41. 0x02="40uS"
  furi_hal_spi_bus_tx(spi, spiBuff, 3, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command

  // Set LoRa Symbol Number timeout
  // How many symbols are needed for a good receive.
  // Symbols are preamble symbols
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0xA0; // Opcode for "SetLoRaSymbNumTimeout"
  spiBuff[1] = 0x00; // Number of symbols. Ping-pong example from Semtech uses 5
  furi_hal_spi_bus_tx(spi, spiBuff, 2, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command

  // Enable interrupts
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x08; // 0x08 is the opcode for "SetDioIrqParams"
  spiBuff[1] = 0x00; // IRQMask MSB. IRQMask is "what interrupts are enabled"
  spiBuff[2] = 0x02; // IRQMask LSB See datasheet table 13-29 for details
  spiBuff[3] = 0xFF; // DIO1 mask MSB. Of the interrupts detected, which should be triggered on DIO1 pin
  spiBuff[4] = 0xFF; // DIO1 Mask LSB
  spiBuff[5] = 0x00; // DIO2 Mask MSB
  spiBuff[6] = 0x00; // DIO2 Mask LSB
  spiBuff[7] = 0x00; // DIO3 Mask MSB
  spiBuff[8] = 0x00; // DIO3
  
  furi_hal_spi_bus_tx(spi, spiBuff, 9, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  furi_delay_ms(100); // Give time for radio to process the command
}

bool waitForRadioCommandCompletion(uint32_t timeout) {
  uint32_t startTime = furi_get_tick(); // Get the start time in ticks
  bool dataTransmitted = false;

  // Keep checking the radio status until the operation is completed
  while (!dataTransmitted) {
    // Wait a while between SPI status queries to avoid reading too quickly
    furi_delay_ms(5);

    // Request a status update from the radio
    furi_hal_gpio_write(pin_nss1, false); // Enable the radio chip-select
    furi_hal_spi_acquire(spi);


    spiBuff[0] = 0xC0; // Opcode for the "getStatus" command
    spiBuff[1] = 0x00 ; // Dummy byte, status will overwrite this byte
    
    furi_hal_spi_bus_tx(spi, spiBuff, 2, timeout);

    furi_hal_spi_release(spi);
    furi_hal_gpio_write(pin_nss1, true); // Disable the radio chip-select

    // Parse the status
    uint8_t chipMode = (spiBuff[1] >> 4) & 0x07;      // Chip mode is bits [6:4] (3-bits)
    uint8_t commandStatus = (spiBuff[1] >> 1) & 0x07; // Command status is bits [3:1] (3-bits)

    // Check if the operation has finished
	  //Status 0, 1, 2 mean we're still busy.  Anything else means we're done.
    //Commands 3-6 = command timeout, command processing error, failure to execute command, and Tx Done (respoectively)
    if (commandStatus != 0 && commandStatus != 1 && commandStatus != 2) {
      dataTransmitted = true;
    }

    // If we are in standby mode, there's no need to wait anymore
    if (chipMode == 0x03 || chipMode == 0x02) {
      dataTransmitted = true;
    }

    // Prevent infinite loop by implementing a timeout
    if ((furi_get_tick() - startTime) >= furi_ms_to_ticks(timeout)) {
      return false;
    }
  }

  // Success!
  return true;
}

//Sets the radio into receive mode, allowing it to listen for incoming packets.
//If radio is already in receive mode, this does nothing.
//There's no such thing as "setModeTransmit" because it is set automatically when transmit() is called
void setModeReceive() {
  if (inReceiveMode) { return; }  // We're already in receive mode, this would do nothing

  // Set packet parameters
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x8C;          //Opcode for "SetPacketParameters"
  spiBuff[1] = 0x00;          //PacketParam1 = Preamble Len MSB
  spiBuff[2] = 0x0C;          //PacketParam2 = Preamble Len LSB
  spiBuff[3] = 0x00;          //PacketParam3 = Header Type. 0x00 = Variable Len, 0x01 = Fixed Length
  spiBuff[4] = 0xFF;          //PacketParam4 = Payload Length (Max is 255 bytes)
  spiBuff[5] = 0x00;          //PacketParam5 = CRC Type. 0x00 = Off, 0x01 = on
  spiBuff[6] = 0x00;          //PacketParam6 = Invert IQ.  0x00 = Standard, 0x01 = Inverted
  
  furi_hal_spi_bus_tx(spi, spiBuff, 7, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  waitForRadioCommandCompletion(100);

  // Tell the chip to wait for it to receive a packet.
  // Based on our previous config, this should throw an interrupt when we get a packet
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x82;          //0x82 is the opcode for "SetRX"
  spiBuff[1] = 0xFF;          //24-bit timeout, 0xFFFFFF means no timeout
  spiBuff[2] = 0xFF;          // ^^
  spiBuff[3] = 0xFF;          // ^^

  furi_hal_spi_bus_tx(spi, spiBuff, 4, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  waitForRadioCommandCompletion(100);

  // Remember that we're in receive mode so we don't need to run this code again unnecessarily
  inReceiveMode = true;
}

/*Receive a packet if available
If available, this will return the size of the packet and store the packet contents into the user-provided buffer.
A max length of the buffer can be provided to avoid buffer overflow.  If buffer is not large enough for entire payload, overflow is thrown out.
Recommended to pass in a buffer that is 255 bytes long to make sure you can received any lora packet that comes in.

Returns -1 when no packet is available.
Returns 0 when an empty packet is received (packet with no payload)
Returns payload size (1-255) when a packet with a non-zero payload is received. If packet received is larger than the buffer provided, this will return buffMaxLen
*/
int lora_receive_async(u_int8_t* buff, int buffMaxLen) {
  setModeReceive(); // Sets the mode to receive (if not already in receive mode)

  // Radio pin DIO1 (interrupt) goes high when we have a packet ready. If it's low, there's no packet yet
  if (!furi_hal_gpio_read(pin_dio1)) { return -1; } // Return -1, meaning no packet ready

  // Tell the radio to clear the interrupt, and set the pin back inactive.
  while (furi_hal_gpio_read(pin_dio1)) {
    // Clear all interrupt flags. This should result in the interrupt pin going low
    furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
    furi_hal_spi_acquire(spi);

    spiBuff[0] = 0x02;          //Opcode for ClearIRQStatus command
    spiBuff[1] = 0xFF;          //IRQ bits to clear (MSB) (0xFFFF means clear all interrupts)
    spiBuff[2] = 0xFF;          //IRQ bits to clear (LSB)
	
    furi_hal_spi_bus_tx(spi, spiBuff, 3, timeout);

    furi_hal_spi_release(spi);
    furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
  }

  // (Optional) Read the packet status info from the radio.
  // This provides debug info about the packet we received
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x14;          //Opcode for get packet status
  spiBuff[1] = 0xFF;          //Dummy byte. Returns status
  spiBuff[2] = 0xFF;          //Dummy byte. Returns rssi
  spiBuff[3] = 0xFF;          //Dummy byte. Returns snd
  spiBuff[4] = 0xFF;          //Dummy byte. Returns signal RSSI

  furi_hal_spi_bus_tx(spi, spiBuff, 5, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select

  // Store these values as class variables so they can be accessed if needed
  // Documentation for what these variables mean can be found in the .h file
  rssi       = -((int)spiBuff[2]) / 2;  // "Average over last packet received of RSSI. Actual signal power is –RssiPkt/2 (dBm)"
  snr        =  ((int8_t)spiBuff[3]) / 4; // SNR is returned as a SIGNED byte, so we need to do some conversion first
  signalRssi = -((int)spiBuff[4]) / 2;

  // We're almost ready to read the packet from the radio
  // But first we have to know how big the packet is, and where in the radio memory it is stored
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x13;          //Opcode for GetRxBufferStatus command
  spiBuff[1] = 0xFF;          //Dummy.  Returns radio status
  spiBuff[2] = 0xFF;          //Dummy.  Returns loraPacketLength
  spiBuff[3] = 0xFF;          //Dummy.  Returns memory offset (address)

  furi_hal_spi_bus_tx(spi, spiBuff, 4, timeout);

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select

  uint8_t payloadLen = spiBuff[2];   // How long the lora packet is
  uint8_t startAddress = spiBuff[3]; // Where in 1262 memory is the packet stored

  // Make sure we don't overflow the buffer if the packet is larger than our buffer
  if (buffMaxLen < payloadLen) {payloadLen = buffMaxLen;}

  // Read the radio buffer from the SX1262 into the user-supplied buffer
  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  furi_hal_spi_acquire(spi);

  spiBuff[0] = 0x1E;         // Opcode for ReadBuffer command
  spiBuff[1] = startAddress; // SX1262 memory location to start reading from
  spiBuff[2] = 0x00;         // Dummy byte
  furi_hal_spi_bus_tx(spi, spiBuff, 3, timeout); // Send commands to get read started
  furi_hal_spi_bus_rx(spi, buff, payloadLen, timeout); // Get the contents from the radio and store it into the user provided buffer

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select

  return payloadLen; // Return how many bytes we actually read
}

/* Tests that SPI is communicating correctly with the radio.
* If this fails, check your SPI wiring.  This does not require any setup to run.
* We test the radio by reading a register that should have a known value.
*
* Returns: True if radio is communicating over SPI. False if no connection.
*/
bool sanityCheck() {

    uint8_t command_read_register[1] = {0x1D}; // OpCode for "read register"
    uint8_t read_register_address[2] = {0x07,0x40};
    uint8_t dummy_byte = 0x00;
    uint8_t regValue;

    furi_hal_spi_acquire(spi);

    if(furi_hal_spi_bus_tx(spi, command_read_register, 1, timeout) &&
       furi_hal_spi_bus_tx(spi, read_register_address, 2, timeout) &&
       furi_hal_spi_bus_tx(spi, &dummy_byte, 1, timeout) &&
       furi_hal_spi_bus_rx(spi, &regValue, 1, timeout)) {

        FURI_LOG_E(TAG,"REGISTER VALUE: %02x",regValue);
        furi_hal_spi_release(spi);
        return regValue == 0x14; // Success if we read 0x14 from the register
    } else {

        FURI_LOG_E(TAG, "FAILED - furi_hal_spi_bus_tx or furi_hal_spi_bus_rx failed.");
        furi_hal_spi_release(spi);
        return false;
    }
}

void checkBusy()
{
  uint8_t busy_timeout_cnt;
  busy_timeout_cnt = 0;

  furi_hal_gpio_init_simple(pin_busy, GpioModeInput);

  while (furi_hal_gpio_read(pin_busy))
  {
    furi_delay_ms(1);
    busy_timeout_cnt++;

    if (busy_timeout_cnt > 10) //wait 10mS for busy to complete
    {
      busy_timeout_cnt = 0;
      FURI_LOG_E(TAG,"ERROR - Busy Timeout!");
      break;
    }
  }
}

bool begin() {
    
    //furi_hal_gpio_init(pin_reset, GpioModeOutputPushPull, GpioPullUp, GpioSpeedVeryHigh);
    //furi_hal_gpio_init(pin_nss1, GpioModeOutputPushPull, GpioPullUp, GpioSpeedVeryHigh);

    furi_hal_gpio_init_simple(pin_reset, GpioModeOutputPushPull);
    furi_hal_gpio_init_simple(pin_nss1, GpioModeOutputPushPull);

    furi_hal_gpio_write(pin_nss1, true);
    furi_hal_gpio_write(pin_reset, true);

    furi_hal_gpio_init_simple(pin_dio1, GpioModeInput);

    FURI_LOG_E(TAG,"RESET DEVICE...");
    furi_delay_ms(10);
    furi_hal_gpio_write(pin_reset, false);
    furi_delay_ms(2);
    furi_hal_gpio_write(pin_reset, true);
    furi_delay_ms(25);

    checkBusy();

    //furi_hal_gpio_init(pin_dio1,GpioModeInput,GpioPullUp,GpioSpeedLow);

    //Ensure SPI communication is working with the radio
    FURI_LOG_E(TAG,"SANITYCHECK...");
    bool success = sanityCheck();
    if (!success) { return false; }

    //Run the bare-minimum required SPI commands to set up the radio to use
    configureRadioEssentials();
  
    return true;  //Return success that we set up the radio
}

void readRegisters(uint16_t address, uint8_t *buffer, uint16_t size)
{
  uint16_t index;
  uint8_t addr_l, addr_h;

  addr_h = address >> 8;
  addr_l = address & 0x00FF;
  checkBusy();

  furi_hal_gpio_write(pin_nss1, false); // Enable radio chip-select
  
  furi_hal_spi_acquire(spi);

  spiBuff[0] = RADIO_READ_REGISTER;
  spiBuff[1] =  addr_h;
  spiBuff[1] =  addr_l;
  spiBuff[2] =  0xFF; 
  
  furi_hal_spi_bus_tx(spi, spiBuff, 4, timeout);

  for (index = 0; index < size; index++)
  {
    furi_hal_spi_bus_rx(spi, buffer + index, 1, timeout);
  }

  furi_hal_spi_release(spi);
  furi_hal_gpio_write(pin_nss1, true); // Disable radio chip-select
}

uint8_t readRegister(uint16_t address)
{
  uint8_t data;

  readRegisters(address, &data, 1);
  return data;
}

uint16_t getSyncWord()
{
  uint8_t msb, lsb;
  uint16_t syncword;
  msb = readRegister(REG_LR_SYNCWORD);
  lsb = readRegister(REG_LR_SYNCWORD + 1);
  syncword = (msb << 8) + lsb;

  return syncword;
}