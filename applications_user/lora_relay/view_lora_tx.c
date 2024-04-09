#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

#include "view_lora_tx.h"

#define PATHAPP "apps_data/lora"
#define PATHAPPEXT EXT_PATH(PATHAPP)
#define PATHLORA PATHAPPEXT "/data.txt"
#define LORA_LOG_FILE_EXTENSION ".log"

#define TAG "TX "

const GpioPin* const tx_led = &gpio_swclk;

void transmit(uint8_t *data, int dataLen);

uint8_t transmitBuff[255];

typedef struct {
    uint32_t test;
    uint32_t size;
    uint32_t counter;
    bool flag_file;
    FuriString* text;
    DialogsApp* dialogs;
    Storage* storage;
    File* file;
} ViewLoRaTXModel;

struct ViewLoRaTX {
    View* view;
    FuriTimer* timer;
};

static void view_lora_tx_draw_callback_intro(Canvas* canvas, void* _model) {
    UNUSED(_model);
    canvas_draw_str(canvas, 12, 24, "Use < and > to switch tests");
    canvas_draw_str(canvas, 12, 36, "Use ^ and v to select LoRa packet");
    canvas_draw_str(canvas, 32, 48, "Use (o) to send LoRa packet");
}

static void view_lora_tx_draw_callback_move(Canvas* canvas, void* _model) {
    ViewLoRaTXModel* model = _model;

    bool flag_file = model->flag_file;

    uint8_t block = 5 + model->size;
    uint8_t width = canvas_width(canvas) - block;
    uint8_t height = canvas_height(canvas) - block;

    uint8_t x = model->counter % width;
    if((model->counter / width) % 2) {
        x = width - x;
    }

    uint8_t y = model->counter % height;
    if((model->counter / height) % 2) {
        y = height - y;
    }

    if(flag_file) {
        furi_hal_gpio_write(tx_led, true);
        furi_delay_ms(50);
        furi_hal_gpio_write(tx_led, false);
    }

    canvas_draw_box(canvas, x, y, block, block);

    canvas_draw_str(canvas, 12, 12, "HELL TX...");

    int bytesRead = 8;
    if(flag_file) {
        storage_file_write(model->file, transmitBuff, bytesRead);
        storage_file_write(model->file, "\n", 1);
    }


    canvas_draw_str(canvas, 12, 36, "ASCII:");
}

const ViewDrawCallback view_lora_tx_tests[] = {
    view_lora_tx_draw_callback_intro,
    // view_lora_tx_draw_callback_fill,
    // view_lora_tx_draw_callback_hstripe,
    // view_lora_tx_draw_callback_vstripe,
    // view_lora_tx_draw_callback_check,
    view_lora_tx_draw_callback_move,
};

static void view_lora_tx_draw_callback(Canvas* canvas, void* _model) {
    ViewLoRaTXModel* model = _model;
    view_lora_tx_tests[model->test](canvas, _model);
}

static bool view_lora_tx_input_callback(InputEvent* event, void* context) {
    ViewLoRaTX* instance = context;

    bool consumed = false;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        with_view_model(
            instance->view,
            ViewLoRaTXModel * model,
            {
                if(event->key == InputKeyLeft && model->test > 0) {
                    model->test--;
                    consumed = true;
                } else if(
                    event->key == InputKeyRight &&
                    model->test < (COUNT_OF(view_lora_tx_tests) - 1)) {
                    model->test++;
                    consumed = true;
                } else if(event->key == InputKeyDown && model->size > 0) {
                    model->size--;
                    consumed = true;
                } else if(event->key == InputKeyUp && model->size < 24) {
                    model->size++;
                    consumed = true;
                } else if(event->key == InputKeyOk) {
                    //model->flag_file = !model->flag_file;

                    // if(model->flag_file) {
                    //     storage_file_open(model->file, PATHLORA, FSAM_WRITE, FSOM_CREATE_ALWAYS);
                    //     FURI_LOG_E(TAG,"OPEN FILE ");
                    // }
                    // else {
                    //     storage_file_close(model->file);
                    //     FURI_LOG_E(TAG,"CLOSE FILE ");
                    // }

                    FuriString* predefined_filepath = furi_string_alloc_set_str(PATHAPP);
                    FuriString* selected_filepath = furi_string_alloc();
                    DialogsFileBrowserOptions browser_options;
                    dialog_file_browser_set_basic_options(&browser_options, LORA_LOG_FILE_EXTENSION, NULL);
                    browser_options.base_path = PATHAPP;
                    dialog_file_browser_show(model->dialogs, selected_filepath, predefined_filepath, &browser_options);

                    if(storage_file_open(
                            model->file, furi_string_get_cstr(selected_filepath), FSAM_READ, FSOM_OPEN_EXISTING)) {

                            FURI_LOG_E(TAG,"OPEN FILE");

                            furi_string_reset(model->text);
                            char buf[storage_file_size(model->file)];

                            FURI_LOG_E(TAG,"SIZE FILE %d", sizeof(buf));

                            storage_file_read(model->file, buf, sizeof(buf));
                            buf[sizeof(buf)] = '\0';

                            furi_string_cat_str(model->text, buf);

                            FURI_LOG_E(TAG,"SEND FILE...");

                            for(uint8_t i = 0,j = 0; i < sizeof(buf); i++, j++) {
                                
                                //uint8_t packet[]
                                transmitBuff[j] = buf[i];
                                if(buf[i] == '\n') {
                                    transmitBuff[j] = '\0';
                                    FURI_LOG_E(TAG,"SEND PACKET... %s", (char *)transmitBuff);
                                    j = 0;
                                    i++;
                                }

                            }

                            

                        } else {
                            dialog_message_show_storage_error(model->dialogs, "Cannot open File");
                            return false;
                        }
                        storage_file_close(model->file);
                        FURI_LOG_E(TAG,"CLOSE FILE...");
                        furi_string_free(selected_filepath);
                        furi_string_free(predefined_filepath);


                    consumed = true;
                }
            },
            consumed);
    }

    return consumed;
}

static void view_lora_tx_enter(void* context) {
    ViewLoRaTX* instance = context;
    furi_timer_start(instance->timer, furi_kernel_get_tick_frequency() / 32);

    // Initialize the LED pin as output.
    // GpioModeOutputPushPull means true = 3.3 volts, false = 0 volts.
    // GpioModeOutputOpenDrain means true = floating, false = 0 volts.
    furi_hal_gpio_init_simple(tx_led, GpioModeOutputPushPull);
}

static void view_lora_tx_exit(void* context) {
    ViewLoRaTX* instance = context;
    furi_timer_stop(instance->timer);
}

static void view_lora_tx_timer_callback(void* context) {
    ViewLoRaTX* instance = context;
    with_view_model(
        instance->view, ViewLoRaTXModel * model, { model->counter++; }, true);
}

ViewLoRaTX* view_lora_tx_alloc() {
    ViewLoRaTX* instance = malloc(sizeof(ViewLoRaTX));

    instance->view = view_alloc();
    view_set_context(instance->view, instance);
    view_allocate_model(instance->view, ViewModelTypeLockFree, sizeof(ViewLoRaTXModel));

    ViewLoRaTXModel* model = view_get_model(instance->view);

    model->dialogs = furi_record_open(RECORD_DIALOGS);
    model->storage = furi_record_open(RECORD_STORAGE);
    model->file = storage_file_alloc(model->storage);
    model->text = furi_string_alloc();

    view_set_draw_callback(instance->view, view_lora_tx_draw_callback);
    view_set_input_callback(instance->view, view_lora_tx_input_callback);
    view_set_enter_callback(instance->view, view_lora_tx_enter);
    view_set_exit_callback(instance->view, view_lora_tx_exit);

    instance->timer =
        furi_timer_alloc(view_lora_tx_timer_callback, FuriTimerTypePeriodic, instance);

    return instance;
}

void view_lora_tx_free(ViewLoRaTX* instance) {
    furi_assert(instance);

    ViewLoRaTXModel* model = view_get_model(instance->view);

    storage_file_free(model->file);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);
    furi_string_free(model->text);

    furi_timer_free(instance->timer);
    view_free(instance->view);
    free(instance);
}

View* view_lora_tx_get_view(ViewLoRaTX* instance) {
    furi_assert(instance);
    return instance->view;
}
