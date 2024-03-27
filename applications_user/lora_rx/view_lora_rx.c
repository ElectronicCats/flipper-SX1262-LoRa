#include <furi.h>
#include <furi_hal.h>
#include <storage/storage.h>

#include "view_lora_rx.h"

#define TAG "RX "

const GpioPin* const test_led = &gpio_swclk;

int lora_receive_async(u_int8_t* buff, int buffMaxLen);

uint8_t receiveBuff[255];
char asciiBuff[255];

typedef struct {
    uint32_t test;
    uint32_t size;
    uint32_t counter;
    bool flip_flop;
} ViewLoRaRXModel;

struct ViewLoRaRX {
    View* view;
    FuriTimer* timer;
};

static void view_lora_rx_draw_callback_intro(Canvas* canvas, void* _model) {
    UNUSED(_model);
    canvas_draw_str(canvas, 12, 24, "Use < and > to switch tests");
    canvas_draw_str(canvas, 12, 36, "Use ^ and v to switch size");
    canvas_draw_str(canvas, 32, 48, "Use (o) to flip");
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

    bool flip_flop = model->flip_flop;

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

    if(flip_flop) {
        furi_hal_gpio_write(test_led, true);
        furi_delay_ms(50);
        furi_hal_gpio_write(test_led, false);
    }

    canvas_draw_box(canvas, x, y, block, block);

    canvas_draw_str(canvas, 12, 12, "Packet received...");

    //Receive a packet over radio
    int bytesRead = lora_receive_async(receiveBuff, sizeof(receiveBuff));

    if (bytesRead > -1) {
        FURI_LOG_E(TAG,"Packet received... ");
        receiveBuff[bytesRead] = '\0';        

        FURI_LOG_E(TAG,"%s",receiveBuff);  
        bytesToAscii(receiveBuff, 16);
        asciiBuff[17] = '.';
        asciiBuff[18] = '.';
        asciiBuff[19] = '.';
        asciiBuff[20] = '\0';    
    }


    canvas_draw_str(canvas, 12, 24, asciiBuff);
    canvas_draw_str(canvas, 12, 36, "ASCII:");
    canvas_draw_str(canvas, 12, 48, (const char*)receiveBuff);//(char*)receiveBuff);
    

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
                    model->size++;
                    consumed = true;
                } else if(event->key == InputKeyOk) {
                    model->flip_flop = !model->flip_flop;
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

    furi_timer_free(instance->timer);
    view_free(instance->view);
    free(instance);
}

View* view_lora_rx_get_view(ViewLoRaRX* instance) {
    furi_assert(instance);
    return instance->view;
}
