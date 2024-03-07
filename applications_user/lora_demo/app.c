#include <furi.h>
#include <gui/gui.h>
#include <furi_hal.h>

#define TAG "LoRa test"

static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

static void my_draw_callback(Canvas* canvas, void* context) {
    UNUSED(context);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str(canvas, 5, 8, "LoRa Test");
    canvas_draw_str(canvas, 5, 22, "Connect Electronic Cats");
    canvas_draw_str(canvas, 5, 32, "Add-On SubGHz");
}

void abandone();
void configureRadioEssentials();
bool begin();
bool sanityCheck();
void checkBusy();
int lora_receive_async(u_int8_t* buff, int buffMaxLen);

uint8_t receiveBuff[255];

int32_t main_lora(void* _p) {
    UNUSED(_p);

    // Show directions to user.
    Gui* gui = furi_record_open(RECORD_GUI);
    ViewPort* view_port = view_port_alloc();
    view_port_draw_callback_set(view_port, my_draw_callback, NULL);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    uint32_t freq_tick;
    
    freq_tick = furi_kernel_get_tick_frequency();

    FURI_LOG_E(TAG,"Frequency tick %ld", freq_tick);

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

    while(furi_hal_gpio_read(pin_back)) {
        //Receive a packet over radio
        int bytesRead = lora_receive_async(receiveBuff, sizeof(receiveBuff));

        if (bytesRead > -1) {
            FURI_LOG_E(TAG,"Packet received... ");
            //Serial.write(receiveBuff,bytesRead);
        
        }
    }

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    // Remove the directions from the screen.
    gui_remove_view_port(gui, view_port);
    return 0;
}