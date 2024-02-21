#include <furi.h>
#include <gui/gui.h>
#include <furi_hal.h>

#define TAG "SPI LoRa test"

static uint32_t timeout = 1000;
static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_nss1 = &gpio_ext_pc0;
const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

uint8_t command_regfrmsb[1] = {0x06}; // REGFRMSB
uint8_t command_regfrmid[1] = {0x07}; // REGFRMID
uint8_t command_regfrlsb[1] = {0x08}; // REGFRLSB

uint8_t data_response[2] = {0, 0}; // Read 2 bytes

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "GPIO - SPI DEMO");
    canvas_draw_str(canvas, 5, 22, "Connect Electronic Cats");
    canvas_draw_str(canvas, 5, 32, "Add-On SubGHz");
}
//void spi_demo();

int32_t main_demo_spi(void* _p) {
    UNUSED(_p);

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    //spi_demo();
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++
    furi_hal_gpio_init_simple(pin_nss1, GpioModeOutputPushPull);
    furi_hal_gpio_write(pin_nss1, false);

    furi_hal_spi_acquire(spi);

    if(furi_hal_spi_bus_tx(spi, command_regfrmid, 1, timeout) &&
       (furi_hal_spi_bus_rx(spi, data_response, 2, timeout))) {
        char* data_response_str = (char*)data_response;
        FURI_LOG_E(
            TAG,
            "ID CHIP IS: %s",
            data_response_str);
    } else {
        FURI_LOG_E(TAG, "FAILED - furi_hal_spi_bus_tx or furi_hal_spi_bus_rx failed.");
    }

    furi_hal_spi_release(spi);

    //SPI.transfer(address | 0x80);              //mask address for write

    furi_hal_gpio_write(pin_nss1, true);
    //++++++++++++++++++++++++++++++++++++++++++++++++++++++++++


    // Initialize the LED pin as output.
    // GpioModeOutputPushPull means true = 3.3 volts, false = 0 volts.
    // GpioModeOutputOpenDrain means true = floating, false = 0 volts.
    furi_hal_gpio_init_simple(pin_led, GpioModeOutputPushPull);
    do {
        furi_hal_gpio_write(pin_led, true);
        furi_delay_ms(500);
        furi_hal_gpio_write(pin_led, false);
        furi_delay_ms(500);

        // Hold the back button to exit (since we only scan it when restarting loop).
    } while(furi_hal_gpio_read(pin_back));

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);
    return 0;
}