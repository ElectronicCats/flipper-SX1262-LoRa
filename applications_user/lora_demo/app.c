#include <furi.h>
#include <gui/gui.h>
#include <furi_hal.h>

#define TAG "LoRa test"

static uint32_t timeout = 1000;
static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

//const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_led = &gpio_swclk;

const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_reset = &gpio_ext_pc1;
const GpioPin* const pin_ant_sw = &gpio_usart_tx;
const GpioPin* const pin_busy = &gpio_usart_rx;
const GpioPin* const pin_dio1 = &gpio_ext_pc3;


const GpioPin* const pin_back = &gpio_button_back;

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "LoRa Test");
    canvas_draw_str(canvas, 5, 22, "Connect Electronic Cats");
    canvas_draw_str(canvas, 5, 32, "Add-On SubGHz");
}

void abandone();
bool begin();
bool sanityCheck();
void checkBusy();

uint8_t spiBuff[32];   //Buffer for sending SPI commands to radio

//Config variables (set to PRESET_DEFAULT on init)
uint32_t pllFrequency;
uint8_t bandwidth;
uint8_t codingRate;
uint8_t spreadingFactor;
uint8_t lowDataRateOptimize;
uint32_t transmitTimeout; //Worst-case transmit time depends on some factors

int32_t main_lora(void* _p) {
    UNUSED(_p);

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);


    FURI_LOG_E(TAG,"PA4: %32x", (unsigned)&gpio_ext_pa4);
    FURI_LOG_E(TAG,"PC0: %32x", (unsigned)&gpio_ext_pc0);

    FURI_LOG_E(TAG,"spi->cs: %32x", (unsigned)spi->cs);

    spi->cs = &gpio_ext_pc0;

    FURI_LOG_E(TAG,"spi->cs after: %32x", (unsigned)spi->cs);

    //spi->cs = &gpio_ext_pa4;


    //spi->cs = &gpio_ext_pc0;
    
    furi_hal_spi_bus_handle_init(spi);

    abandone();

    begin();

    // sanityCheck();
    // furi_delay_ms(1000);

    furi_hal_spi_bus_handle_deinit(spi);
    
    spi->cs = &gpio_ext_pa4;


    // Initialize the LED pin as output.
    // GpioModeOutputPushPull means true = 3.3 volts, false = 0 volts.
    // GpioModeOutputOpenDrain means true = floating, false = 0 volts.
    furi_hal_gpio_init_simple(pin_led, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_led, true);
    furi_delay_ms(500);
    furi_hal_gpio_write(pin_led, false);
    furi_delay_ms(500);

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);
    return 0;
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
    //configureRadioEssentials();
  
    return true;  //Return success that we set up the radio
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

/*Convert a frequency in hz (such as 915000000) to the respective PLL setting.
* The radio requires that we set the PLL, which controls the multipler on the internal clock to achieve the desired frequency.
* Valid frequencies are 150mhz to 960mhz (150000000 to 960000000)
*
* NOTE: This assumes the radio is using a 32mhz clock, which is standard.  This is independent of the microcontroller clock
* See datasheet section 13.4.1 for this calculation.
* Example: 915mhz (915000000) has a PLL of 959447040
*/
uint32_t frequencyToPLL(long rfFreq) {
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
  //Make sure the specified frequency is in the valid range.
  if (frequencyInHz < 150000000 || frequencyInHz > 960000000) { return false;}

  //Calculate the PLL frequency (See datasheet section 13.4.1 for calculation)
  //PLL frequency controls the radio's clock multipler to achieve the desired frequency
  pllFrequency = frequencyToPLL(frequencyInHz);
  updateRadioFrequency();
  return true;
}

/*Send the bare-bones required commands needed for radio to run.
* Do not set custom or optional commands here, please keep this section as simplified as possible.
* Essential commands are found by reading the datasheet
*/
void configureRadioEssentials() {
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
  // configSetPreset(PRESET_DEFAULT); // Sets default modulation parameters

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

