#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>
#include <dialogs/dialogs.h>

#include "view_lora_rx.h"
#include "lora_relay_icons.h"

#define PATHAPP "apps_data/lora"
#define PATHAPPEXT EXT_PATH(PATHAPP)
#define PATHLORA PATHAPPEXT "/data.log"
#define LORA_LOG_FILE_EXTENSION ".log"

#define TAG "RX "

const GpioPin* const test_led = &gpio_swclk;

int lora_receive_async(u_int8_t* buff, int buffMaxLen);

uint8_t receiveBuff[255];
char asciiBuff[255];

typedef struct {
    uint32_t test;
    uint32_t size;
    uint32_t counter;
    bool flag_file;
    DialogsApp* dialogs;
    Storage* storage;
    File* file;
} ViewLoRaRXModel;

struct ViewLoRaRX {
    View* view;
    FuriTimer* timer;
};

static void view_lora_rx_draw_callback_intro(Canvas* canvas, void* _model) {
    UNUSED(_model);
    
    canvas_draw_icon(canvas, 0, 0, &I_sam_flipper);
    canvas_draw_str(canvas, 12, 6, "Use > to start sniffing");
}

void bytesToAscii(uint8_t* buffer, uint8_t length) {
    uint8_t i;
    for (i = 0; i < length; ++i) {
        asciiBuff[i * 2] = "0123456789ABCDEF"[buffer[i] >> 4]; // High nibble
        asciiBuff[i * 2 + 1] = "0123456789ABCDEF"[buffer[i] & 0x0F]; // Low nibble
    }
    asciiBuff[length * 2] = '\0'; // Null-terminate the string
    FURI_LOG_E(TAG,"OUT bytesToAscii ");
}

static void view_lora_rx_draw_callback_move(Canvas* canvas, void* _model) {
    ViewLoRaRXModel* model = _model;

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

    canvas_draw_box(canvas, x, y, block, block);

    //Receive a packet over radio
    int bytesRead = lora_receive_async(receiveBuff, sizeof(receiveBuff));

    if (bytesRead > -1) {
        FURI_LOG_E(TAG,"Packet received... ");
        receiveBuff[bytesRead] = '\0';
        
        if(flag_file) {
            storage_file_write(model->file, receiveBuff, bytesRead);
            storage_file_write(model->file, "\n", 1);
            
        }

        //FURI_LOG_E(TAG,"flag_file = %d",(int)flag_file);

        FURI_LOG_E(TAG,"%s",receiveBuff);  
        bytesToAscii(receiveBuff, 16);
        asciiBuff[17] = '.';
        asciiBuff[18] = '.';
        asciiBuff[19] = '.';
        asciiBuff[20] = '\0';    
    }

    if(flag_file) {
        canvas_draw_icon(canvas, 100, 6, &I_write);
    }
    else {
        canvas_draw_icon(canvas, 100, 6, &I_no_write);
    }

    canvas_draw_str(canvas, 0, 8, "Use (o) to start/stop");
    canvas_draw_str(canvas, 0, 16, "recording");
    canvas_draw_str(canvas, 0, 30, "Packet received...");
    canvas_draw_str(canvas, 0, 40, asciiBuff);
    canvas_draw_str(canvas, 0, 52, "ASCII:");
    canvas_draw_str(canvas, 0, 62, (const char*)receiveBuff);//(char*)receiveBuff);
    

    //receiveBuff[0] = '\0';

}

const ViewDrawCallback view_lora_rx_tests[] = {
    view_lora_rx_draw_callback_intro,
    // view_lora_rx_draw_callback_fill,
    // view_lora_rx_draw_callback_hstripe,
    // view_lora_rx_draw_callback_vstripe,
    // view_lora_rx_draw_callback_check,
    view_lora_rx_draw_callback_move,
};

static void view_lora_rx_draw_callback(Canvas* canvas, void* _model) {
    ViewLoRaRXModel* model = _model;
    view_lora_rx_tests[model->test](canvas, _model);
}

static bool view_lora_rx_input_callback(InputEvent* event, void* context) {
    ViewLoRaRX* instance = context;

    bool consumed = false;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        with_view_model(
            instance->view,
            ViewLoRaRXModel * model,
            {
                if(event->key == InputKeyLeft && model->test > 0) {
                    model->test--;
                    consumed = true;
                } else if(
                    event->key == InputKeyRight &&
                    model->test < (COUNT_OF(view_lora_rx_tests) - 1)) {
                    model->test++;
                    consumed = true;
                } else if(event->key == InputKeyDown && model->size > 0) {
                    model->size--;
                    consumed = true;
                } else if(event->key == InputKeyUp && model->size < 24) {

                    // FuriString* predefined_filepath = furi_string_alloc_set_str(PATHAPP);
                    // FuriString* selected_filepath = furi_string_alloc();
                    // DialogsFileBrowserOptions browser_options;
                    // dialog_file_browser_set_basic_options(&browser_options, LORA_LOG_FILE_EXTENSION, NULL);
                    // browser_options.base_path = PATHAPP;
                    // dialog_file_browser_show(model->dialogs, selected_filepath, predefined_filepath, &browser_options);

                    model->size++;
                    consumed = true;
                } else if(event->key == InputKeyOk) {
                    model->flag_file = !model->flag_file;

                    if(model->flag_file) {
                        storage_file_open(model->file, PATHLORA, FSAM_WRITE, FSOM_CREATE_ALWAYS);
                        FURI_LOG_E(TAG,"OPEN FILE ");
                    }
                    else {
                        storage_file_close(model->file);
                        FURI_LOG_E(TAG,"CLOSE FILE ");
                    }
                    consumed = true;
                }
            },
            consumed);
    }

    return consumed;
}

static void view_lora_rx_enter(void* context) {
    ViewLoRaRX* instance = context;
    furi_timer_start(instance->timer, furi_kernel_get_tick_frequency() / 32);

    // Initialize the LED pin as output.
    // GpioModeOutputPushPull means true = 3.3 volts, false = 0 volts.
    // GpioModeOutputOpenDrain means true = floating, false = 0 volts.
    furi_hal_gpio_init_simple(test_led, GpioModeOutputPushPull);
}

static void view_lora_rx_exit(void* context) {
    ViewLoRaRX* instance = context;
    furi_timer_stop(instance->timer);
}

static void view_lora_rx_timer_callback(void* context) {
    ViewLoRaRX* instance = context;
    with_view_model(
        instance->view, ViewLoRaRXModel * model, { model->counter++; }, true);
}

ViewLoRaRX* view_lora_rx_alloc() {
    ViewLoRaRX* instance = malloc(sizeof(ViewLoRaRX));

    instance->view = view_alloc();
    view_set_context(instance->view, instance);
    
    view_allocate_model(instance->view, ViewModelTypeLockFree, sizeof(ViewLoRaRXModel));

    ViewLoRaRXModel* model = view_get_model(instance->view);

    model->dialogs = furi_record_open(RECORD_DIALOGS);
    model->storage = furi_record_open(RECORD_STORAGE);
    model->file = storage_file_alloc(model->storage);

    view_set_draw_callback(instance->view, view_lora_rx_draw_callback);
    view_set_input_callback(instance->view, view_lora_rx_input_callback);
    view_set_enter_callback(instance->view, view_lora_rx_enter);
    view_set_exit_callback(instance->view, view_lora_rx_exit);

    instance->timer =
        furi_timer_alloc(view_lora_rx_timer_callback, FuriTimerTypePeriodic, instance);

    return instance;
}

void view_lora_rx_free(ViewLoRaRX* instance) {
    furi_assert(instance);

    ViewLoRaRXModel* model = view_get_model(instance->view);

    storage_file_free(model->file);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    furi_timer_free(instance->timer);
    view_free(instance->view);
    free(instance);
}

View* view_lora_rx_get_view(ViewLoRaRX* instance) {
    furi_assert(instance);
    return instance->view;
}
