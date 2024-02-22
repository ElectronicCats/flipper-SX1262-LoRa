#include <furi.h>
#include <gui/gui.h>
#include <furi_hal.h>

#define TAG "SPI LoRa test"

static uint32_t timeout = 1000;
static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

//const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "LoRa Test");
    canvas_draw_str(canvas, 5, 22, "Connect Electronic Cats");
    canvas_draw_str(canvas, 5, 32, "Add-On SubGHz");
}

bool sanityCheck();

int32_t main_demo_spi(void* _p) {
    UNUSED(_p);

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    furi_hal_spi_bus_handle_init(spi);

    sanityCheck();

    furi_hal_spi_bus_handle_deinit(spi);
    

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

    //spi->cs = &gpio_ext_pc0;

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
