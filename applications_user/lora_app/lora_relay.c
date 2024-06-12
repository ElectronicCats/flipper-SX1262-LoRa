#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <locale/locale.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/byte_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include "lora_app_icons.h"

#define PATHAPP "apps_data/lora"
#define PATHAPPEXT EXT_PATH(PATHAPP)
#define PATHLORA PATHAPPEXT "/data.log"
#define LORA_LOG_FILE_EXTENSION ".log"

#define TIME_LEN 12
#define DATE_LEN 14

#define CLOCK_TIME_FORMAT "%.2d:%.2d:%.2d"
#define CLOCK_ISO_DATE_FORMAT "%.4d-%.2d-%.2d"

static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

#define TAG "LoRa"

uint8_t receiveBuff[255];
char asciiBuff[255];

void abandone();
int16_t getRSSI();
void configureRadioEssentials();
bool begin();
bool sanityCheck();
void checkBusy();
void setModeReceive();
int lora_receive_async(u_int8_t* buff, int buffMaxLen);
bool configSetFrequency(long frequencyInHz);
bool configSetBandwidth(int bw);
bool configSetSpreadingFactor(int sf);
void setPacketParams(uint16_t packetParam1, uint8_t packetParam2, uint8_t packetParam3, uint8_t packetParam4, uint8_t packetParam5);

void transmit(uint8_t *data, int dataLen);

// Change this to BACKLIGHT_AUTO if you don't want the backlight to be continuously on.
#define BACKLIGHT_ON 1

// Our application menu has 3 items.  You can add more items if you want.
typedef enum {
    LoRaSubmenuIndexConfigure,
    LoRaSubmenuIndexSniffer,
    LoRaSubmenuIndexTransmitter,
    LoRaSubmenuIndexManualTX,
    LoRaSubmenuIndexAbout,
} LoRaSubmenuIndex;

// Each view is a screen we show the user.
typedef enum {
    LoRaViewSubmenu, // The menu when the app starts
    LoRaViewFrequencyInput, // Input for configuring frequency settings
    //LoRaViewPayloadLenInput, // Input for configuring payload length settings
    LoRaViewByteInput, // Input for send data (bytes)
    LoRaViewConfigure, // The configuration screen
    LoRaViewSniffer, // Sniffer
    LoraViewTransmitter, // Transmitter
    LoRaViewAbout, // The about screen with directions, link to social channel, etc.
} LoRaView;

typedef enum {
    LoRaEventIdRedrawScreen = 0, // Custom event to redraw the screen
    LoRaEventIdOkPressed = 42, // Custom event to process OK button getting pressed down
} LoRaEventId;

typedef struct {
    ViewDispatcher* view_dispatcher; // Switches between our views
    NotificationApp* notifications; // Used for controlling the backlight
    Submenu* submenu; // The application menu
    TextInput* frequency_input; // The text input screen
    ByteInput* byte_input; // The byte input screen
    //ByteInput* payload_len_input; // The payload length input screen
    VariableItemList* variable_item_list_config; // The configuration screen
    View* view_sniffer; // The main screen
    View* view_transmitter; // The other main screen
    Widget* widget_about; // The about screen

    VariableItem* config_freq_item; // The name setting item (so we can update the text)
    char* temp_buffer; // Temporary buffer for text input
    uint32_t temp_buffer_size; // Size of temporary buffer

    uint8_t* byte_buffer; // Temporary buffer for text input
    uint32_t byte_buffer_size; // Size of temporary buffer

    FuriTimer* timer; // Timer for redrawing the screen

    int config_frequency;

    // Order is preamble, header type, packet length, CRC, IQ
    uint16_t packetPreamble;
    uint8_t packetHeaderType;
    uint8_t packetPayloadLength;
    uint8_t packetCRC;
    uint8_t packetInvertIQ;
  
} LoRaApp;

typedef struct {
    FuriString* config_freq_name; // The name setting    
    uint32_t config_bw_index; // Bandwidth setting index
    uint32_t config_sf_index; // Spread Factor setting index
    uint32_t config_header_type_index; // Spread Factor setting index
    uint32_t config_crc_index; // Spread Factor setting index
    uint32_t config_iq_index; // Spread Factor setting index

    uint8_t x; // The x coordinate

    bool flag_file;
    DialogsApp* dialogs_rx;
    Storage* storage_rx;
    File* file_rx;
} LoRaSnifferModel;

typedef struct {
    uint32_t test;
    bool flag_tx_file;
    bool flag_signal;
    FuriString* text;
    DialogsApp* dialogs_tx;
    Storage* storage_tx;
    File* file_tx;
    uint8_t x; // The x coordinate
} LoRaTransmitterModel;

/**
 * @brief      Callback for exiting the application.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to exit the application.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t lora_navigation_exit_callback(void* _context) {
    UNUSED(_context);
    return VIEW_NONE;
}

/**
 * @brief      Callback for returning to submenu.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the submenu.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t lora_navigation_submenu_callback(void* _context) {
    UNUSED(_context);
    return LoRaViewSubmenu;
}

/**
 * @brief      Callback for returning to configure screen.
 * @details    This function is called when user press back button.  We return VIEW_NONE to
 *            indicate that we want to navigate to the configure screen.
 * @param      _context  The context - unused
 * @return     next view id
*/
static uint32_t lora_navigation_configure_callback(void* _context) {
    UNUSED(_context);
    return LoRaViewConfigure;
}

/**
 * @brief      Handle submenu item selection.
 * @details    This function is called when user selects an item from the submenu.
 * @param      context  The context - LoRaApp object.
 * @param      index     The LoRaSubmenuIndex item that was clicked.
*/
static void lora_submenu_callback(void* context, uint32_t index) {
    LoRaApp* app = (LoRaApp*)context;
    switch(index) {
    case LoRaSubmenuIndexConfigure:
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewConfigure);
        break;
    case LoRaSubmenuIndexSniffer:
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewSniffer);
        break;
    case LoRaSubmenuIndexTransmitter:
        view_dispatcher_switch_to_view(app->view_dispatcher, LoraViewTransmitter);
        break; 
    case LoRaSubmenuIndexManualTX:
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewByteInput);
        break;       
    case LoRaSubmenuIndexAbout:
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewAbout);
        break;
    default:
        break;
    }
}

const uint8_t config_bw_values[] = {
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
const char* const config_bw_names[] = {
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

const uint8_t config_sf_values[] = {
    0x05,
    0x06,
    0x07,
    0x08,
    0x09,
    0x0A,
    0x0B,
    0x0C,
};
const char* const config_sf_names[] = {
    "SF5",
    "SF6",
    "SF7",
    "SF8",
    "SF9",
    "SF10",
    "SF11",
    "SF12",
};

//Header Type. 0x00 = Variable Len, 0x01 = Fixed Length
const uint8_t config_header_type_values[] = {
    0x00,
    0x01,
};
const char* const config_header_type_names[] = {
    "Variable Len",
    "Fixed Length",
};

//CRC. 0x00 = Off, 0x01 = On
const uint8_t config_crc_values[] = {
    0x00,
    0x01,
};
const char* const config_crc_names[] = {
    "Off",
    "On",
};

//IQ. 0x00 = Standard, 0x01 = Inverted
const uint8_t config_iq_values[] = {
    0x00,
    0x01,
};
const char* const config_iq_names[] = {
    "Standard",
    "Inverted",
};

static const char* config_bw_config_label = "Bandwidth";

static void lora_config_bw_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_bw_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_bw_index = index;

    configSetBandwidth(config_bw_values[index]);

}

static const char* config_sf_config_label = "Spread Factor";

static void lora_config_sf_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_sf_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_sf_index = index;

    configSetSpreadingFactor(config_sf_values[index]);
}

static const char* config_header_type_config_label = "Header Type";

static void lora_config_header_type_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_header_type_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_header_type_index = index;

    app->packetHeaderType = config_header_type_values[index];

    // Order is preamble, header type, packet length, CRC, IQ
    setPacketParams(app->packetPreamble, app->packetHeaderType, app->packetPayloadLength, app->packetCRC, app->packetInvertIQ);

}

static const char* config_crc_config_label = "CRC";

static void lora_config_crc_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_crc_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_crc_index = index;

    app->packetCRC = config_crc_values[index];

    // Order is preamble, header type, packet length, CRC, IQ
    setPacketParams(app->packetPreamble, app->packetHeaderType, app->packetPayloadLength, app->packetCRC, app->packetInvertIQ);

}

static const char* config_iq_config_label = "IQ";

static void lora_config_iq_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_iq_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_iq_index = index;

    app->packetInvertIQ = config_iq_values[index];

    // Order is preamble, header type, packet length, CRC, IQ
    setPacketParams(app->packetPreamble, app->packetHeaderType, app->packetPayloadLength, app->packetCRC, app->packetInvertIQ);

}

/**
 * Our 2nd sample setting is a text field.  When the user clicks OK on the configuration 
 * setting we use a text input screen to allow the user to enter a frequency.  This function is
 * called when the user clicks OK on the text input screen.
*/
static const char* config_freq_config_label = "Frequency";
static const char* config_freq_entry_text = "Enter frequency (MHz)";
static const char* config_freq_default_value = "915.0";
static void lora_config_freq_text_updated(void* context) {
    LoRaApp* app = (LoRaApp*)context;
    bool redraw = true;
    with_view_model(
        app->view_sniffer,
        LoRaSnifferModel * model,
        {
            furi_string_set(model->config_freq_name, app->temp_buffer);
            variable_item_set_current_value_text(
                app->config_freq_item, furi_string_get_cstr(model->config_freq_name));

            const char* freq_str = furi_string_get_cstr(model->config_freq_name);
            app->config_frequency = (int)(strtof(freq_str, NULL)*1000000);
            FURI_LOG_E(TAG,"Frequency = %d", app->config_frequency);
            
        },
        redraw);


    view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewConfigure);
    configSetFrequency(app->config_frequency);
}


static void SetValue(void* context) {
    LoRaApp* app = (LoRaApp*)context;

    FURI_LOG_E(TAG, "Byte buffer: %s", (char *)app->byte_buffer);
    view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewSubmenu);
    transmit(app->byte_buffer, app->byte_buffer_size);
}

/**
 * @brief      Callback when item in configuration screen is clicked.
 * @details    This function is called when user clicks OK on an item in the configuration screen.
 *            If the item clicked is our text field then we switch to the text input screen.
 * @param      context  The context - LoRaApp object.
 * @param      index - The index of the item that was clicked.
*/
static void lora_setting_item_clicked(void* context, uint32_t index) {
    LoRaApp* app = (LoRaApp*)context;
    index++; // The index starts at zero, but we want to start at 1.

    // Our configuration UI has the 2nd item as a text field.
    if(index == 1) {
        // Header to display on the text input screen.
        text_input_set_header_text(app->frequency_input, config_freq_entry_text);

        // Copy the current name into the temporary buffer.
        bool redraw = false;
        with_view_model(
            app->view_sniffer,
            LoRaSnifferModel * model,
            {
                strncpy(
                    app->temp_buffer,
                    furi_string_get_cstr(model->config_freq_name),
                    app->temp_buffer_size);
            },
            redraw);

        // Configure the text input.  When user enters text and clicks OK, lora_setting_text_updated be called.
        bool clear_previous_text = false;
        text_input_set_result_callback(
            app->frequency_input,
            lora_config_freq_text_updated,
            app,
            app->temp_buffer,
            app->temp_buffer_size,
            clear_previous_text);

        // Pressing the BACK button will reload the configure screen.
        view_set_previous_callback(
            text_input_get_view(app->frequency_input), lora_navigation_configure_callback);

        // Show text input dialog.
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewFrequencyInput);
    }
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

/**
 * @brief      Callback for drawing the sniffer screen.
 * @details    This function is called when the screen needs to be redrawn, like when the model gets updated.
 * @param      canvas  The canvas to draw on.
 * @param      model   The model - MyModel object.
*/
static void lora_view_sniffer_draw_callback(Canvas* canvas, void* model) {
    LoRaSnifferModel* my_model = (LoRaSnifferModel*)model;

    bool flag_file = my_model->flag_file;

    canvas_draw_icon(canvas, my_model->x, 20, &I_glyph_1_14x40);

        //Receive a packet over radio
    int bytesRead = lora_receive_async(receiveBuff, sizeof(receiveBuff));

    if (bytesRead > -1) {
        FURI_LOG_E(TAG,"Packet received... ");
        receiveBuff[bytesRead] = '\0';
        
        if(flag_file) {

            FuriHalRtcDateTime curr_dt;
            furi_hal_rtc_get_datetime(&curr_dt);

            char time_string[TIME_LEN];
            char date_string[DATE_LEN];

            snprintf(time_string, TIME_LEN, CLOCK_TIME_FORMAT, curr_dt.hour, curr_dt.minute, curr_dt.second);
            snprintf(date_string, DATE_LEN, CLOCK_ISO_DATE_FORMAT, curr_dt.year, curr_dt.month, curr_dt.day);

            char final_string[400];
            const char* freq_str = furi_string_get_cstr(my_model->config_freq_name);
            snprintf(final_string, 400, "{\"date\":\"%s\", \"time\":\"%s\", \"frequency\":\"%s\", \"bw\":\"%s\", \"sf\":\"%s\", \"RSSI\":\"%d\", \"payload\":\"%s\"}", date_string, time_string, freq_str, config_bw_names[my_model->config_bw_index], config_sf_names[my_model->config_sf_index],getRSSI(),receiveBuff);

            FURI_LOG_E(TAG, "TS: %s", final_string);
            FURI_LOG_E(TAG, "Length: %d", strlen(final_string) + 1);  

            storage_file_write(my_model->file_rx, final_string, strlen(final_string));
            storage_file_write(my_model->file_rx, "\n", 1);
            
        }

        FURI_LOG_E(TAG,"%s",receiveBuff);  
        bytesToAscii(receiveBuff, 16);
        asciiBuff[17] = '.';
        asciiBuff[18] = '.';
        asciiBuff[19] = '.';
        asciiBuff[20] = '\0';    
    }
    //canvas_draw_str(canvas, 1, 10, "LEFT/RIGHT to change x");
    canvas_draw_str(canvas, 1, 10, (const char*)receiveBuff);

    FuriString* xstr = furi_string_alloc();

    furi_string_printf(xstr, "RSSI: %d  ", getRSSI());
    canvas_draw_str(canvas, 60, 10, furi_string_get_cstr(xstr));

    furi_string_printf(xstr, "x: %u  OK=play tone", my_model->x);
    canvas_draw_str(canvas, 44, 24, furi_string_get_cstr(xstr));

    furi_string_printf(xstr, "random: %u", (uint8_t)(furi_hal_random_get() % 256));
    canvas_draw_str(canvas, 44, 36, furi_string_get_cstr(xstr));
    furi_string_printf(
        xstr,
        "Bandwidth: %s (%u)",
        config_bw_names[my_model->config_bw_index],
        config_bw_values[my_model->config_bw_index]);
    canvas_draw_str(canvas, 44, 48, furi_string_get_cstr(xstr));
    furi_string_printf(xstr, "name: %s", furi_string_get_cstr(my_model->config_freq_name));
    canvas_draw_str(canvas, 44, 60, furi_string_get_cstr(xstr));
    furi_string_free(xstr);
}

/**
 * @brief      Callback for drawing the transmitter screen.
 * @details    This function is called when the screen needs to be redrawn, like when the model gets updated.
 * @param      canvas  The canvas to draw on.
 * @param      model   The model - MyModel object.
*/
static void lora_view_transmitter_draw_callback(Canvas* canvas, void* model) {
    LoRaTransmitterModel* my_model = (LoRaTransmitterModel*)model;

    canvas_draw_icon(canvas, my_model->x, 20, &I_glyph_1_14x40);

    canvas_draw_str(canvas, 1, 10, "LEFT/RIGHT to change x");

    FuriString* xstr = furi_string_alloc();
    furi_string_printf(xstr, "x: %u  OK=play tone", my_model->x);
    canvas_draw_str(canvas, 44, 24, furi_string_get_cstr(xstr));

    furi_string_printf(xstr, "random: %u", (uint8_t)(furi_hal_random_get() % 256));
    canvas_draw_str(canvas, 44, 36, furi_string_get_cstr(xstr));
    
    furi_string_free(xstr);
}

/**
 * @brief      Callback for timer elapsed.
 * @details    This function is called when the timer is elapsed.  We use this to queue a redraw event.
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_sniffer_timer_callback(void* context) {
    LoRaApp* app = (LoRaApp*)context;
    view_dispatcher_send_custom_event(app->view_dispatcher, LoRaEventIdRedrawScreen);
}

/**
 * @brief      Callback for timer elapsed.
 * @details    This function is called when the timer is elapsed.  We use this to queue a redraw event.
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_transmitter_timer_callback(void* context) {
    LoRaApp* app = (LoRaApp*)context;
    view_dispatcher_send_custom_event(app->view_dispatcher, LoRaEventIdRedrawScreen);
}

/**
 * @brief      Callback when the user starts the sniffer screen.
 * @details    This function is called when the user enters the sniffer screen.  We start a timer to
 *           redraw the screen periodically (so the random number is refreshed).
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_sniffer_enter_callback(void* context) {
    uint32_t period = furi_ms_to_ticks(200);
    LoRaApp* app = (LoRaApp*)context;
    furi_assert(app->timer == NULL);
    app->timer =
        furi_timer_alloc(lora_view_sniffer_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(app->timer, period);
}

/**
 * @brief      Callback when the user starts the transmitter screen.
 * @details    This function is called when the user enters the transmitter screen.  We start a timer to
 *           redraw the screen periodically (so the random number is refreshed).
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_transmitter_enter_callback(void* context) {
    uint32_t period = furi_ms_to_ticks(1000);
    LoRaApp* app = (LoRaApp*)context;
    furi_assert(app->timer == NULL);
    app->timer =
        furi_timer_alloc(lora_view_transmitter_timer_callback, FuriTimerTypePeriodic, context);
    furi_timer_start(app->timer, period);
}

/**
 * @brief      Callback when the user exits the sniffer screen.
 * @details    This function is called when the user exits the sniffer screen.  We stop the timer.
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_sniffer_exit_callback(void* context) {
    LoRaApp* app = (LoRaApp*)context;
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->timer = NULL;
}

/**
 * @brief      Callback when the user exits the transmitter screen.
 * @details    This function is called when the user exits the transmitter screen.  We stop the timer.
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_transmitter_exit_callback(void* context) {
    LoRaApp* app = (LoRaApp*)context;
    furi_timer_stop(app->timer);
    furi_timer_free(app->timer);
    app->timer = NULL;
}

/**
 * @brief      Callback for custom events.
 * @details    This function is called when a custom event is sent to the view dispatcher.
 * @param      event    The event id - LoRaEventId value.
 * @param      context  The context - LoRaApp object.
*/
static bool lora_view_sniffer_custom_event_callback(uint32_t event, void* context) {
    LoRaApp* app = (LoRaApp*)context;
    switch(event) {
    case LoRaEventIdRedrawScreen:
        // Redraw screen by passing true to last parameter of with_view_model.
        {
            bool redraw = true;
            with_view_model(
                app->view_sniffer, LoRaSnifferModel * _model, { UNUSED(_model); }, redraw);
            return true;
        }
    case LoRaEventIdOkPressed:
        // Process the OK button.  We play a tone based on the x coordinate.
        if(furi_hal_speaker_acquire(500)) {
            float frequency;
            bool redraw = false;
            with_view_model(
                app->view_sniffer,
                LoRaSnifferModel * model,
                { frequency = model->x * 100 + 100; },
                redraw);
            furi_hal_speaker_start(frequency, 1.0);
            furi_delay_ms(100);
            furi_hal_speaker_stop();
            furi_hal_speaker_release();
        }
        return true;
    default:
        return false;
    }
}

/**
 * @brief      Callback for custom events.
 * @details    This function is called when a custom event is sent to the view dispatcher.
 * @param      event    The event id - LoRaEventId value.
 * @param      context  The context - LoRaApp object.
*/
static bool lora_view_transmitter_custom_event_callback(uint32_t event, void* context) {
    LoRaApp* app = (LoRaApp*)context;
    switch(event) {
    case LoRaEventIdRedrawScreen:
        // Redraw screen by passing true to last parameter of with_view_model.
        {
            bool redraw = true;
            with_view_model(
                app->view_transmitter, LoRaTransmitterModel * _model, { UNUSED(_model); }, redraw);
            return true;
        }
    case LoRaEventIdOkPressed:
        // Process the OK button.  We play a tone based on the x coordinate.
        if(furi_hal_speaker_acquire(500)) {
            float frequency;
            bool redraw = false;
            with_view_model(
                app->view_transmitter,
                LoRaTransmitterModel * model,
                { frequency = model->x * 100 + 100; },
                redraw);
            furi_hal_speaker_start(frequency, 1.0);
            furi_delay_ms(100);
            furi_hal_speaker_stop();
            furi_hal_speaker_release();
        }
        return true;
    default:
        return false;
    }
}

/**
 * @brief      Callback for sniffer screen input.
 * @details    This function is called when the user presses a button while on the sniffer screen.
 * @param      event    The event - InputEvent object.
 * @param      context  The context - LoRaApp object.
 * @return     true if the event was handled, false otherwise.
*/
static bool lora_view_sniffer_input_callback(InputEvent* event, void* context) {
    LoRaApp* app = (LoRaApp*)context;
    if(event->type == InputTypeShort) {
        if(event->key == InputKeyLeft) {
            // Left button clicked, reduce x coordinate.
            bool redraw = true;
            with_view_model(
                app->view_sniffer,
                LoRaSnifferModel * model,
                {
                    if(model->x > 0) {
                        model->x--;
                    }
                },
                redraw);
        } else if(event->key == InputKeyRight) {
            // Right button clicked, increase x coordinate.
            bool redraw = true;
            with_view_model(
                app->view_sniffer,
                LoRaSnifferModel * model,
                {
                    // Should we have some maximum value?
                    model->x++;
                },
                redraw);
        }
    } else if(event->type == InputTypePress) {
        if(event->key == InputKeyOk) {
            // We choose to send a custom event when user presses OK button.  lora_custom_event_callback will
            // handle our LoRaEventIdOkPressed event.  We could have just put the code from
            // lora_custom_event_callback here, it's a matter of preference.

            bool redraw = true;
            with_view_model(
                app->view_sniffer,
                LoRaSnifferModel * model,
                {
                    // Start/Stop recording
                    model->flag_file = !model->flag_file;

                    if(model->flag_file) {
                        storage_file_open(model->file_rx, PATHLORA, FSAM_WRITE, FSOM_CREATE_ALWAYS);
                        FURI_LOG_E(TAG,"OPEN FILE ");
                    }
                    else {
                        storage_file_close(model->file_rx);
                        FURI_LOG_E(TAG,"CLOSE FILE ");
                    }
                },
                redraw);

            view_dispatcher_send_custom_event(app->view_dispatcher, LoRaEventIdOkPressed);
            return true;
        }
    }

    return false;
}

void pito(void* context) {

    LoRaApp* app = (LoRaApp*)context;
    LoRaTransmitterModel* model = view_get_model(app->view_transmitter);

    uint8_t transmitBuff[64];
    FuriString* predefined_filepath = furi_string_alloc_set_str(PATHAPP);
    FuriString* selected_filepath = furi_string_alloc();
    DialogsFileBrowserOptions browser_options;
    dialog_file_browser_set_basic_options(&browser_options, LORA_LOG_FILE_EXTENSION, NULL);
    browser_options.base_path = PATHAPP;

    dialog_file_browser_show(model->dialogs_tx, selected_filepath, predefined_filepath, &browser_options);

    if(storage_file_open(
            model->file_tx, furi_string_get_cstr(selected_filepath), FSAM_READ, FSOM_OPEN_EXISTING)) {

            model->flag_tx_file = true;
            model->test = 1;

            //furi_string_reset(model->text);
            char buf[storage_file_size(model->file_tx)];
            
            storage_file_read(model->file_tx, buf, sizeof(buf));
            buf[sizeof(buf)] = '\0';

            uint16_t maxlen = sizeof(buf);
            
            for(uint16_t i = 0,j = 0; i < maxlen; i++,j++) {
                
                transmitBuff[j] = buf[i];
                if(buf[i] == '\n') {
                    transmitBuff[j] = '\0';
                    transmit(transmitBuff, j);
                    furi_delay_ms(10);
                    j = 0;
                    i++;
                }
            }
    } else {
        dialog_message_show_storage_error(model->dialogs_tx, "Cannot open File");
    }
    storage_file_close(model->file_tx);
    model->test = 0;
    furi_string_free(selected_filepath);
    furi_string_free(predefined_filepath);

    furi_hal_gpio_write(pin_led, true);
    furi_delay_ms(50);
    furi_hal_gpio_write(pin_led, false);

    model->flag_tx_file = false;

}

/**
 * @brief      Callback for sniffer screen input.
 * @details    This function is called when the user presses a button while on the transmitter screen.
 * @param      event    The event - InputEvent object.
 * @param      context  The context - LoRaApp object.
 * @return     true if the event was handled, false otherwise.
*/
static bool lora_view_transmitter_input_callback(InputEvent* event, void* context) {
    LoRaApp* app = (LoRaApp*)context;

    bool consumed = false;
    if(event->type == InputTypeShort || event->type == InputTypeRepeat) {
        with_view_model(
            app->view_transmitter,
            LoRaTransmitterModel * model,
            {
                if(event->key == InputKeyLeft && model->test > 0) {
                    //model->test--;
                    consumed = true;
                } else if(
                    event->key == InputKeyRight) { //&&
                    //model->test < (COUNT_OF(view_lora_tx_tests) - 1)) {
                    //model->test++;
                    consumed = true;
                } else if(event->key == InputKeyDown) { //&& model->size > 0) {
                    //model->size--;
                    consumed = true;
                } else if(event->key == InputKeyUp) { //&& model->size < 24) {
                    //model->size++;
                    consumed = true;
                } else if(event->key == InputKeyOk) {
                    pito(app);
                    consumed = true;
                }
            },
            consumed);
    }

    return consumed;
}

static void lora_app_config_set_payload_length(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    FuriString* temp;
    temp = furi_string_alloc();
    furi_string_cat_printf(temp, "%d", index);
    variable_item_set_current_value_text(item, furi_string_get_cstr(temp));
    furi_string_free(temp);
    app->packetPayloadLength = index;

    app->byte_buffer_size = index;
    free(app->byte_buffer);
    app->byte_buffer = (uint8_t*)malloc(app->byte_buffer_size);

    byte_input_set_result_callback(
        app->byte_input, SetValue, NULL, app, app->byte_buffer, app->byte_buffer_size); 

    // Order is preamble, header type, packet length, CRC, IQ
    setPacketParams(app->packetPreamble, app->packetHeaderType, app->packetPayloadLength, app->packetCRC, app->packetInvertIQ);

}


/**
 * @brief      Allocate the LoRa application.
 * @details    This function allocates the LoRa application resources.
 * @return     LoRaApp object.
*/
static LoRaApp* lora_app_alloc() {
    LoRaApp* app = (LoRaApp*)malloc(sizeof(LoRaApp));
    VariableItem* item;
    Gui* gui = furi_record_open(RECORD_GUI);

    app->view_dispatcher = view_dispatcher_alloc();
    view_dispatcher_enable_queue(app->view_dispatcher);
    view_dispatcher_attach_to_gui(app->view_dispatcher, gui, ViewDispatcherTypeFullscreen);
    view_dispatcher_set_event_callback_context(app->view_dispatcher, app);

    app->submenu = submenu_alloc();
    submenu_add_item(
        app->submenu, "Config", LoRaSubmenuIndexConfigure, lora_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Sniffer", LoRaSubmenuIndexSniffer, lora_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Transmitter", LoRaSubmenuIndexTransmitter, lora_submenu_callback, app);
    submenu_add_item(
        app->submenu, "Send LoRa byte", LoRaSubmenuIndexManualTX, lora_submenu_callback, app);   
    submenu_add_item(
        app->submenu, "About", LoRaSubmenuIndexAbout, lora_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), lora_navigation_exit_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewSubmenu);

    app->frequency_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewFrequencyInput, text_input_get_view(app->frequency_input));
    app->temp_buffer_size = 32;
    app->temp_buffer = (char*)malloc(app->temp_buffer_size);

    app->byte_buffer_size = 16;
    app->byte_buffer = (uint8_t*)malloc(app->byte_buffer_size);

    app->byte_input = byte_input_alloc();

    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewByteInput, byte_input_get_view(app->byte_input));

    byte_input_set_header_text(app->byte_input, "Set byte to LoRa TX");
    byte_input_set_result_callback(
        app->byte_input, SetValue, NULL, app, app->byte_buffer, app->byte_buffer_size); 

    app->packetPayloadLength = 16;

    app->variable_item_list_config = variable_item_list_alloc();
    variable_item_list_reset(app->variable_item_list_config);


    FuriString* config_freq_name = furi_string_alloc();
    furi_string_set_str(config_freq_name, config_freq_default_value);
    app->config_freq_item = variable_item_list_add(
        app->variable_item_list_config, config_freq_config_label, 1, NULL, NULL);
    variable_item_set_current_value_text(
        app->config_freq_item, furi_string_get_cstr(config_freq_name));

    // bw
    item = variable_item_list_add(
        app->variable_item_list_config,
        config_bw_config_label,
        COUNT_OF(config_bw_values),
        lora_config_bw_change,
        app);
    uint8_t config_bw_index = 7;
    variable_item_set_current_value_index(item, config_bw_index);
    variable_item_set_current_value_text(item, config_bw_names[config_bw_index]);

    // sf
    item = variable_item_list_add(
        app->variable_item_list_config,
        config_sf_config_label,
        COUNT_OF(config_sf_values),
        lora_config_sf_change,
        app);
    uint8_t config_sf_index = 3;
    variable_item_set_current_value_index(item, config_sf_index);
    variable_item_set_current_value_text(item, config_sf_names[config_sf_index]);

    // Payload length
    item = variable_item_list_add(
        app->variable_item_list_config, "Payload length", 64, lora_app_config_set_payload_length, app);
    variable_item_set_current_value_index(item, 16);
    variable_item_set_current_value_text(item, "16");    

    // Header Type
    item = variable_item_list_add(
        app->variable_item_list_config,
        config_header_type_config_label,
        COUNT_OF(config_header_type_values),
        lora_config_header_type_change,
        app);
    uint8_t config_header_type_index = 0;
    variable_item_set_current_value_index(item, config_header_type_index);
    variable_item_set_current_value_text(item, config_header_type_names[config_header_type_index]);

    // CRC
    item = variable_item_list_add(
        app->variable_item_list_config,
        config_crc_config_label,
        COUNT_OF(config_crc_values),
        lora_config_crc_change,
        app);
    uint8_t config_crc_index = 0;
    variable_item_set_current_value_index(item, config_crc_index);
    variable_item_set_current_value_text(item, config_crc_names[config_crc_index]);

    // Inverted IQ
    item = variable_item_list_add(
        app->variable_item_list_config,
        config_iq_config_label,
        COUNT_OF(config_iq_values),
        lora_config_iq_change,
        app);
    uint8_t config_iq_index = 0;
    variable_item_set_current_value_index(item, config_iq_index);
    variable_item_set_current_value_text(item, config_iq_names[config_iq_index]);

    variable_item_list_set_enter_callback(
        app->variable_item_list_config, lora_setting_item_clicked, app);

    view_set_previous_callback(
        variable_item_list_get_view(app->variable_item_list_config),
        lora_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher,
        LoRaViewConfigure,
        variable_item_list_get_view(app->variable_item_list_config));

    app->view_sniffer = view_alloc();
    view_set_draw_callback(app->view_sniffer, lora_view_sniffer_draw_callback);
    view_set_input_callback(app->view_sniffer, lora_view_sniffer_input_callback);
    view_set_previous_callback(app->view_sniffer, lora_navigation_submenu_callback);
    view_set_enter_callback(app->view_sniffer, lora_view_sniffer_enter_callback);
    view_set_exit_callback(app->view_sniffer, lora_view_sniffer_exit_callback);
    view_set_context(app->view_sniffer, app);
    view_set_custom_callback(app->view_sniffer, lora_view_sniffer_custom_event_callback);
    view_allocate_model(app->view_sniffer, ViewModelTypeLockFree, sizeof(LoRaSnifferModel));
    LoRaSnifferModel* model_s = view_get_model(app->view_sniffer);

    model_s->config_freq_name = config_freq_name;
    model_s->config_bw_index = config_bw_index;
    model_s->config_sf_index = config_sf_index;
    model_s->config_header_type_index = config_header_type_index;
    model_s->config_crc_index = config_crc_index;
    model_s->config_iq_index = config_iq_index;
    
    model_s->x = 0;
    
    model_s->dialogs_rx = furi_record_open(RECORD_DIALOGS);
    model_s->storage_rx = furi_record_open(RECORD_STORAGE);
    model_s->file_rx = storage_file_alloc(model_s->storage_rx);

    view_dispatcher_add_view(app->view_dispatcher, LoRaViewSniffer, app->view_sniffer);

    app->view_transmitter = view_alloc();
    view_set_draw_callback(app->view_transmitter, lora_view_transmitter_draw_callback);
    view_set_input_callback(app->view_transmitter, lora_view_transmitter_input_callback);
    view_set_previous_callback(app->view_transmitter, lora_navigation_submenu_callback);
    view_set_enter_callback(app->view_transmitter, lora_view_transmitter_enter_callback);
    view_set_exit_callback(app->view_transmitter, lora_view_transmitter_exit_callback);
    view_set_context(app->view_transmitter, app);
    view_set_custom_callback(app->view_transmitter, lora_view_transmitter_custom_event_callback);
    view_allocate_model(app->view_transmitter, ViewModelTypeLockFree, sizeof(LoRaTransmitterModel));
    LoRaTransmitterModel* model_t = view_get_model(app->view_transmitter);

    model_t->x = 0;

    model_t->dialogs_tx = furi_record_open(RECORD_DIALOGS);
    model_t->storage_tx = furi_record_open(RECORD_STORAGE);
    model_t->file_tx = storage_file_alloc(model_t->storage_tx);

    view_dispatcher_add_view(app->view_dispatcher, LoraViewTransmitter, app->view_transmitter);

    app->widget_about = widget_alloc();
    widget_add_text_scroll_element(
        app->widget_about,
        0,
        0,
        128,
        64,
        "This is a sample application.\n---\nReplace code and message\nwith your content!\n\nauthor: @codeallnight\nhttps://discord.com/invite/NsjCvqwPAd\nhttps://youtube.com/@MrDerekJamison");
    view_set_previous_callback(
        widget_get_view(app->widget_about), lora_navigation_submenu_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewAbout, widget_get_view(app->widget_about));

    app->notifications = furi_record_open(RECORD_NOTIFICATION);

#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_on);
#endif

    return app;
}

/**
 * @brief      Free the LoRa application.
 * @details    This function frees the LoRa application resources.
 * @param      app  The LoRa application object.
*/
static void lora_app_free(LoRaApp* app) {
#ifdef BACKLIGHT_ON
    notification_message(app->notifications, &sequence_display_backlight_enforce_auto);
#endif
    furi_record_close(RECORD_NOTIFICATION);

    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    storage_file_free(model->file_rx);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    LoRaTransmitterModel* model_t = view_get_model(app->view_transmitter);
    storage_file_free(model_t->file_tx);
    furi_record_close(RECORD_STORAGE);
    furi_record_close(RECORD_DIALOGS);

    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewFrequencyInput);
    text_input_free(app->frequency_input);

    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewByteInput);
    byte_input_free(app->byte_input);

    free(app->temp_buffer);
    free(app->byte_buffer);

    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewAbout);
    widget_free(app->widget_about);
    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewSniffer);
    view_free(app->view_sniffer);
    view_dispatcher_remove_view(app->view_dispatcher, LoraViewTransmitter);
    view_free(app->view_transmitter);
    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewConfigure);
    variable_item_list_free(app->variable_item_list_config);
    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewSubmenu);
    submenu_free(app->submenu);
    view_dispatcher_free(app->view_dispatcher);
    furi_record_close(RECORD_GUI);

    free(app);
}

/**
 * @brief      Main function for LoRa application.
 * @details    This function is the entry point for the LoRa application.  It should be defined in
 *           application.fam as the entry_point setting.
 * @param      _p  Input parameter - unused
 * @return     0 - Success
*/
int32_t main_lora_app(void* _p) {
    UNUSED(_p);

    spi->cs = &gpio_ext_pc0;

    furi_hal_spi_bus_handle_init(spi);

    abandone();

    if(!begin()){

        DialogsApp* dialogs_msg = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_text(
            message,
            "Error!\nSubGHz add-on module failed to start\n\nCheck that the module is plugged in",
            0,
            0,
            AlignLeft,
            AlignTop);
        dialog_message_show(dialogs_msg, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        return 0;
    }    

    LoRaApp* app = lora_app_alloc();

    app->packetPreamble = 0x000C;
    app->packetHeaderType = 0x00;
    app->packetPayloadLength = 0xFF;
    app->packetCRC = 0x00;
    app->packetInvertIQ = 0x00;

    view_dispatcher_run(app->view_dispatcher);

    lora_app_free(app);

    furi_hal_spi_bus_handle_deinit(spi);
    spi->cs = &gpio_ext_pa4;

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    return 0;
}
