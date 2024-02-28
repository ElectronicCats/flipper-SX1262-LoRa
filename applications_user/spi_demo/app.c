#include <furi.h>
#include <gui/gui.h>
#include <furi_hal.h>

#define TAG "SPI LoRa test"

static uint32_t timeout = 1000;
static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

//const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_led = &gpio_swclk;

const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_reset = &gpio_ext_pc1;
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

bool begin();
bool sanityCheck();
void checkBusy();

int32_t main_demo_spi(void* _p) {
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
