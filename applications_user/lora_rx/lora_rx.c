#include "lora_rx.h"

#include <furi_hal.h>
#include <furi.h>

// Need access to u8g2
#include <gui/gui_i.h>
#include <gui/canvas_i.h>
#include <u8g2_glue.h>

#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/variable_item_list.h>

#include "view_lora_rx.h"

#define TAG "LoRaRX"

typedef struct {
    Gui* gui;
    ViewDispatcher* view_dispatcher;
    ViewLoRaRX* view_lora_rx;
    VariableItemList* variable_item_list;
    Submenu* submenu;

    bool config_bw;
    uint8_t config_frequency;
    uint8_t config_sf;
} LoRaRX;

typedef enum {
    LoRaRXViewSubmenu,
    LoRaRXViewConfigure,
    LoRaRXViewLoRaRX,
} LoRaRXView;

const bool config_bw_value[] = {
    true,
    false,
};
const char* const config_bw_text[] = {
    "1/7",
    "1/9",
};

const uint8_t config_sf_value[] = {
    0b000,
    0b001,
    0b010,
    0b011,
    0b100,
    0b101,
    0b110,
    0b111,
};
const char* const config_sf_text[] = {
    "3.0",
    "3.5",
    "4.0",
    "4.5",
    "5.0",
    "5.5",
    "6.0",
    "6.5",
};

static void lora_rx_submenu_callback(void* context, uint32_t index) {
    LoRaRX* instance = (LoRaRX*)context;
    view_dispatcher_switch_to_view(instance->view_dispatcher, index);
}

static uint32_t lora_rx_previous_callback(void* context) {
    UNUSED(context);
    return LoRaRXViewSubmenu;
}

static uint32_t lora_rx_exit_callback(void* context) {
    UNUSED(context);
    return VIEW_NONE;
}

static void lora_rx_reload_config(LoRaRX* instance) {
    FURI_LOG_I(
        TAG,
        "frequency: %d, sf: %d, bw: %d",
        instance->config_frequency,
        instance->config_sf,
        instance->config_bw);
    // u8x8_d_st756x_init(
    //     &instance->gui->canvas->fb.u8x8,
    //     instance->config_frequency,
    //     instance->config_sf,
    //     instance->config_bw);
}

static void lora_config_set_bw(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_bw_text[index]);
    instance->config_bw = config_bw_value[index];
    lora_rx_reload_config(instance);
}

static void lora_config_set_sf(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_sf_text[index]);
    instance->config_sf = config_sf_value[index];
    lora_rx_reload_config(instance);
}

static void lora_config_set_frequency(VariableItem* item) {
    LoRaRX* instance = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    FuriString* temp;
    temp = furi_string_alloc();
    furi_string_cat_printf(temp, "%d", index);
    variable_item_set_current_value_text(item, furi_string_get_cstr(temp));
    furi_string_free(temp);
    instance->config_frequency = index;
    lora_rx_reload_config(instance);
}

LoRaRX* lora_rx_alloc() {
    LoRaRX* instance = malloc(sizeof(LoRaRX));

    View* view = NULL;

    instance->gui = furi_record_open(RECORD_GUI);
    instance->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(instance->view_dispatcher);
    view_dispatcher_attach_to_gui(
        instance->view_dispatcher, instance->gui, ViewDispatcherTypeFullscreen);

    // Test
    instance->view_lora_rx = view_lora_rx_alloc();
    view = view_lora_rx_get_view(instance->view_lora_rx);
    view_set_previous_callback(view, lora_rx_previous_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewLoRaRX, view);

    // Configure
    instance->variable_item_list = variable_item_list_alloc();
    view = variable_item_list_get_view(instance->variable_item_list);
    view_set_previous_callback(view, lora_rx_previous_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewConfigure, view);

    // Configuration items
    VariableItem* item;
    instance->config_bw = false;
    instance->config_frequency = 32;
    instance->config_sf = 0b101;
    // Frequency
    item = variable_item_list_add(
        instance->variable_item_list, "Frequency:", 64, lora_config_set_frequency, instance);
    variable_item_set_current_value_index(item, 32);
    variable_item_set_current_value_text(item, "32");
    // Band Width
    item = variable_item_list_add(
        instance->variable_item_list,
        "Band Width:",
        COUNT_OF(config_bw_value),
        lora_config_set_bw,
        instance);
    variable_item_set_current_value_index(item, 1);
    variable_item_set_current_value_text(item, config_bw_text[1]);
    // Spread Factor
    item = variable_item_list_add(
        instance->variable_item_list,
        "Spread Factor:",
        COUNT_OF(config_sf_value),
        lora_config_set_sf,
        instance);
    variable_item_set_current_value_index(item, 5);
    variable_item_set_current_value_text(item, config_sf_text[5]);

    // Menu
    instance->submenu = submenu_alloc();
    view = submenu_get_view(instance->submenu);
    view_set_previous_callback(view, lora_rx_exit_callback);
    view_dispatcher_add_view(instance->view_dispatcher, LoRaRXViewSubmenu, view);
    submenu_add_item(
        instance->submenu,
        "RX",
        LoRaRXViewLoRaRX,
        lora_rx_submenu_callback,
        instance);
    submenu_add_item(
        instance->submenu,
        "Configure",
        LoRaRXViewConfigure,
        lora_rx_submenu_callback,
        instance);

    return instance;
}

void lora_rx_free(LoRaRX* instance) {
    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewSubmenu);
    submenu_free(instance->submenu);

    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewConfigure);
    variable_item_list_free(instance->variable_item_list);

    view_dispatcher_remove_view(instance->view_dispatcher, LoRaRXViewLoRaRX);
    view_lora_rx_free(instance->view_lora_rx);

    view_dispatcher_free(instance->view_dispatcher);
    furi_record_close(RECORD_GUI);

    free(instance);
}

int32_t lora_rx_run(LoRaRX* instance) {
    UNUSED(instance);
    view_dispatcher_switch_to_view(instance->view_dispatcher, LoRaRXViewSubmenu);
    view_dispatcher_run(instance->view_dispatcher);

    return 0;
}

int32_t lora_rx_app(void* p) {
    UNUSED(p);

    LoRaRX* instance = lora_rx_alloc();

    int32_t ret = lora_rx_run(instance);

    lora_rx_free(instance);

    return ret;
}
