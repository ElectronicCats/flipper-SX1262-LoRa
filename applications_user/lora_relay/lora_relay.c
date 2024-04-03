#include "lora_relay.h"

#include <furi.h>
#include <furi_hal.h>


// Need access to u8g2
#include <gui/gui_i.h>
#include <gui/canvas_i.h>
#include <u8g2_glue.h>

#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>
#include <dialogs/dialogs.h>

#include <storage/storage.h>

#include "view_lora_rx.h"

#define LORA_APP_FOLDER "apps_data/lora"

static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

#define TAG "LoRaRX"

void abandone();
void configureRadioEssentials();
bool begin();
bool sanityCheck();
void checkBusy();
void setModeReceive();
int lora_receive_async(u_int8_t* buff, int buffMaxLen);
bool configSetFrequency(long frequencyInHz);
bool configSetBandwidth(int bw);
bool configSetSpreadingFactor(int sf);

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    ViewLoRaRX* view_lora_rx;

    DialogsApp* dialogs;

    VariableItemList* variable_item_list;
    Submenu* submenu;

    Storage* storage;
    File* capture_file;

    uint8_t config_bw;
    uint8_t config_frequency;
    uint8_t config_sf;
} LoRaRX;

typedef enum {
    LoRaRXViewSubmenu,
    LoRaRXViewConfigure,
    LoRaRXViewLoRaRX,
    // LoRaRXViewLoRaTX,
    // LoRaRXViewLoRaAbout,
} LoRaRXView;

const uint8_t config_bw_value[] = {
    0x00,
    0x08,
    0x01,
    0x09,
    0x02,
    0x0A, 
    0x03,
    0x04,
    0x05,
    0x06,
};
const char* const config_bw_text[] = {
    "7.81 kHz",
    "10.42 kHz",
    "15.63 kHz",
    "20.83 kHz",
    "31.25 kHz",
    "41.67 kHz",
    "62.50 kHz",
    "125 kHz",
    "250 kHz",
    "500 kHz",
};

const uint8_t config_sf_value[] = {
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0x0C,
};
const char* const config_sf_text[] = {
    "SF5",
    "SF6",
    "SF7",
    "SF8",
    "SF9",
    "SF10",
    "SF11",
    "SF12",
};

void lora_make_app_folder(LoRaRX* instance) {
    furi_assert(instance);

    if(!storage_simply_mkdir(instance->storage, LORA_APP_FOLDER)) {
        dialog_message_show_storage_error(instance->dialogs, "Cannot create\napp folder");
    }
}

static void lora_relay_submenu_callback(void* context, uint32_t index) {
    LoRaRX* instance = (LoRaRX*)context;
    view_dispatcher_switch_to_view(instance->view_dispatcher, index);
}

static uint32_t lora_relay_previous_callback(void* context) {
    UNUSED(context);
    return LoRaRXViewSubmenu;
}

static uint32_t lora_relay_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void lora_relay_reload_config(LoRaRX* instance) {
    FURI_LOG_I(
        TAG,
        "frequency: %d, sf: %d, bw: %d",
        instance->config_frequency,
        instance->config_sf,
        instance->config_bw);
}

static void lora_config_set_bw(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_bw_text[index]);
    instance->config_bw = config_bw_value[index];

    configSetBandwidth(config_bw_value[index]);

    lora_relay_reload_config(instance);
}

static void lora_config_set_sf(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_sf_text[index]);
    instance->config_sf = config_sf_value[index];

    configSetSpreadingFactor(config_sf_value[index]);

    lora_relay_reload_config(instance);
}

static void lora_config_set_frequency(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    FuriString* temp;
    temp = furi_string_alloc();
    furi_string_cat_printf(temp, "%d", index + 883);
    variable_item_set_current_value_text(item, furi_string_get_cstr(temp));
    furi_string_free(temp);
    instance->config_frequency = index;

    configSetFrequency((index + 883)*1000000);

    lora_relay_reload_config(instance);
}

LoRaRX* lora_relay_alloc() {
    LoRaRX* instance = malloc(sizeof(LoRaRX));

    View* view = NULL;

    instance->gui = furi_record_open(RECORD_GUI);
    instance->dialogs = furi_record_open(RECORD_DIALOGS);
    instance->storage = furi_record_open(RECORD_STORAGE);

    instance->capture_file = storage_file_alloc(instance->storage);


    instance->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(instance->view_dispatcher);
    view_dispatcher_attach_to_gui(
        instance->view_dispatcher, instance->gui, ViewDispatcherTypeFullscreen);

    // Configure
    instance->variable_item_list = variable_item_list_alloc();
    view = variable_item_list_get_view(instance->variable_item_list);
    view_set_previous_callback(view, lora_relay_previous_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewConfigure, view);

    // RX
    instance->view_lora_rx = view_lora_rx_alloc();
    view = view_lora_rx_get_view(instance->view_lora_rx);
    view_set_previous_callback(view, lora_relay_previous_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewLoRaRX, view);

    // // TX
    // instance->view_lora_tx = view_lora_tx_alloc();
    // view = view_lora_relay_get_view(instance->view_lora_rx);
    // view_set_previous_callback(view, lora_relay_previous_callback);
    // view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewLoRaTX, view);

    // Configuration items
    VariableItem* item;
    instance->config_bw = 0x04;
    instance->config_frequency = 32;
    instance->config_sf = 0x08;
    // Frequency
    item = variable_item_list_add(
        instance->variable_item_list, "Frequency:", 64, lora_config_set_frequency, instance);
    variable_item_set_current_value_index(item, 32);
    variable_item_set_current_value_text(item, "915");
    // Band Width
    item = variable_item_list_add(
        instance->variable_item_list,
        "Band Width:",
        COUNT_OF(config_bw_value),
        lora_config_set_bw,
        instance);
    variable_item_set_current_value_index(item, 7);
    variable_item_set_current_value_text(item, config_bw_text[7]);
    // Spread Factor
    item = variable_item_list_add(
        instance->variable_item_list,
        "Spread Factor:",
        COUNT_OF(config_sf_value),
        lora_config_set_sf,
        instance);
    variable_item_set_current_value_index(item, 3);
    variable_item_set_current_value_text(item, config_sf_text[3]);

    // Menu
    instance->submenu = submenu_alloc();
    view = submenu_get_view(instance->submenu);
    view_set_previous_callback(view, lora_relay_exit_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewSubmenu, view);
    submenu_add_item(
        instance->submenu,
        "LoRa settings",
        LoRaRXViewConfigure,
        lora_relay_submenu_callback,
        instance);
    submenu_add_item(
        instance->submenu,
        "LoRa sniffer",
        LoRaRXViewLoRaRX,
        lora_relay_submenu_callback,
        instance);
    // submenu_add_item(
    //     instance->submenu,
    //     "Transmit Signals",
    //     LoRaRXViewLoRaTX,
    //     lora_relay_submenu_callback,
    //     instance);
    // submenu_add_item(
    //     instance->submenu,
    //     "About",
    //     LoRaRXViewLoRaAbout,
    //     lora_relay_submenu_callback,
    //     instance);    
    return instance;
}

void lora_relay_free(LoRaRX* instance) {
    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewSubmenu);
    submenu_free(instance->submenu);

    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewConfigure);

    storage_file_free(instance->capture_file);
    variable_item_list_free(instance->variable_item_list);

    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewLoRaRX);
    view_lora_rx_free(instance->view_lora_rx);

    // view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewLoRaTX);
    // variable_item_list_free(instance->variable_item_list);

    // view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewLoRaAbout);
    // view_lora_tx_free(instance->view_lora_tx);

    view_dispatcher_free(instance->view_dispatcher);
    furi_record_close(RECORD_GUI);

    free(instance);
}

int32_t lora_relay_run(LoRaRX* instance) {
    UNUSED(instance);
    view_dispatcher_switch_to_view(instance->view_dispatcher, LoRaRXViewSubmenu);
    view_dispatcher_run(instance->view_dispatcher);

    return 0;
}

int32_t lora_relay_app(void* p) {
    UNUSED(p);

    spi->cs = &gpio_ext_pc0;

    furi_hal_spi_bus_handle_init(spi);

    abandone();

    begin();

    LoRaRX* instance = lora_relay_alloc();

    int32_t ret = lora_relay_run(instance);

    lora_relay_free(instance);

    furi_hal_spi_bus_handle_deinit(spi);
    spi->cs = &gpio_ext_pa4;

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    return ret;
}
