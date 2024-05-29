#include <furi.h>
#include <furi_hal.h>
#include <gui/gui.h>
#include <gui/view.h>
#include <gui/view_dispatcher.h>
#include <gui/modules/submenu.h>
#include <gui/modules/text_input.h>
#include <gui/modules/widget.h>
#include <gui/modules/variable_item_list.h>
#include <notification/notification.h>
#include <notification/notification_messages.h>

#include <dialogs/dialogs.h>
#include <storage/storage.h>

#include "lora_app_icons.h"


static FuriHalSpiBusHandle* spi = &furi_hal_spi_bus_handle_external;

const GpioPin* const pin_led = &gpio_swclk;
const GpioPin* const pin_back = &gpio_button_back;

#define TAG "LoRa"

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

// Change this to BACKLIGHT_AUTO if you don't want the backlight to be continuously on.
#define BACKLIGHT_ON 1

// Our application menu has 3 items.  You can add more items if you want.
typedef enum {
    LoRaSubmenuIndexConfigure,
    LoRaSubmenuIndexSniffer,
    LoRaSubmenuIndexAbout,
} LoRaSubmenuIndex;

// Each view is a screen we show the user.
typedef enum {
    LoRaViewSubmenu, // The menu when the app starts
    LoRaViewTextInput, // Input for configuring text settings
    LoRaViewConfigure, // The configuration screen
    LoRaViewSniffer, // The main screen
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
    TextInput* text_input; // The text input screen
    VariableItemList* variable_item_list_config; // The configuration screen
    View* view_sniffer; // The main screen
    Widget* widget_about; // The about screen

    VariableItem* config_freq_item; // The name setting item (so we can update the text)
    char* temp_buffer; // Temporary buffer for text input
    uint32_t temp_buffer_size; // Size of temporary buffer

    FuriTimer* timer; // Timer for redrawing the screen
} LoRaApp;

typedef struct {
    FuriString* config_freq_name; // The name setting    
    uint32_t config_bw_index; // The team color setting index
    uint32_t config_sf_index; // The team color setting index

    uint8_t x; // The x coordinate
} LoRaSnifferModel;

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

/**
 * Our 1st sample setting is a team color.  We have 3 options: red, green, and blue.
*/
static const char* config_bw_config_label = "Bandwidth";

static void lora_config_bw_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_bw_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_bw_index = index;
}

static const char* config_sf_config_label = "Spread Factor";

static void lora_config_sf_change(VariableItem* item) {
    LoRaApp* app = variable_item_get_context(item);
    uint8_t index = variable_item_get_current_value_index(item);
    variable_item_set_current_value_text(item, config_sf_names[index]);
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);
    model->config_sf_index = index;
}

/**
 * Our 2nd sample setting is a text field.  When the user clicks OK on the configuration 
 * setting we use a text input screen to allow the user to enter a name.  This function is
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
            float freq_value = strtof(freq_str, NULL);
            FURI_LOG_E(TAG, "abandon hope all ye who enter here");
            FURI_LOG_E(TAG,"Frequency = %d", (int)(freq_value*1000000));
            configSetFrequency((int)(freq_value*1000000));
        },
        redraw);
    view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewConfigure);
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
        text_input_set_header_text(app->text_input, config_freq_entry_text);

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
            app->text_input,
            lora_config_freq_text_updated,
            app,
            app->temp_buffer,
            app->temp_buffer_size,
            clear_previous_text);

        // Pressing the BACK button will reload the configure screen.
        view_set_previous_callback(
            text_input_get_view(app->text_input), lora_navigation_configure_callback);

        // Show text input dialog.
        view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewTextInput);
    }
}

/**
 * @brief      Callback for drawing the sniffer screen.
 * @details    This function is called when the screen needs to be redrawn, like when the model gets updated.
 * @param      canvas  The canvas to draw on.
 * @param      model   The model - MyModel object.
*/
static void lora_view_sniffer_draw_callback(Canvas* canvas, void* model) {
    LoRaSnifferModel* my_model = (LoRaSnifferModel*)model;
    canvas_draw_icon(canvas, my_model->x, 20, &I_glyph_1_14x40);
    canvas_draw_str(canvas, 1, 10, "LEFT/RIGHT to change x");
    FuriString* xstr = furi_string_alloc();
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
 * @brief      Callback for timer elapsed.
 * @details    This function is called when the timer is elapsed.  We use this to queue a redraw event.
 * @param      context  The context - LoRaApp object.
*/
static void lora_view_sniffer_timer_callback(void* context) {
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
            view_dispatcher_send_custom_event(app->view_dispatcher, LoRaEventIdOkPressed);
            return true;
        }
    }

    return false;
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
        app->submenu, "Play", LoRaSubmenuIndexSniffer, lora_submenu_callback, app);
    submenu_add_item(
        app->submenu, "About", LoRaSubmenuIndexAbout, lora_submenu_callback, app);
    view_set_previous_callback(submenu_get_view(app->submenu), lora_navigation_exit_callback);
    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewSubmenu, submenu_get_view(app->submenu));
    view_dispatcher_switch_to_view(app->view_dispatcher, LoRaViewSubmenu);

    app->text_input = text_input_alloc();
    view_dispatcher_add_view(
        app->view_dispatcher, LoRaViewTextInput, text_input_get_view(app->text_input));
    app->temp_buffer_size = 32;
    app->temp_buffer = (char*)malloc(app->temp_buffer_size);

    app->variable_item_list_config = variable_item_list_alloc();
    variable_item_list_reset(app->variable_item_list_config);


    FuriString* config_freq_name = furi_string_alloc();
    furi_string_set_str(config_freq_name, config_freq_default_value);
    app->config_freq_item = variable_item_list_add(
        app->variable_item_list_config, config_freq_config_label, 1, NULL, NULL);
    variable_item_set_current_value_text(
        app->config_freq_item, furi_string_get_cstr(config_freq_name));


    item = variable_item_list_add(
        app->variable_item_list_config,
        config_bw_config_label,
        COUNT_OF(config_bw_values),
        lora_config_bw_change,
        app);
    uint8_t config_bw_index = 7;
    variable_item_set_current_value_index(item, config_bw_index);
    variable_item_set_current_value_text(item, config_bw_names[config_bw_index]);

    item = variable_item_list_add(
        app->variable_item_list_config,
        config_sf_config_label,
        COUNT_OF(config_sf_values),
        lora_config_sf_change,
        app);
    uint8_t config_sf_index = 3;
    variable_item_set_current_value_index(item, config_sf_index);
    variable_item_set_current_value_text(item, config_sf_names[config_sf_index]);




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
    LoRaSnifferModel* model = view_get_model(app->view_sniffer);

    model->config_freq_name = config_freq_name;
    model->config_bw_index = config_bw_index;
    model->config_sf_index = config_sf_index;
    
    model->x = 0;
    view_dispatcher_add_view(app->view_dispatcher, LoRaViewSniffer, app->view_sniffer);

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

    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewTextInput);
    text_input_free(app->text_input);
    free(app->temp_buffer);
    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewAbout);
    widget_free(app->widget_about);
    view_dispatcher_remove_view(app->view_dispatcher, LoRaViewSniffer);
    view_free(app->view_sniffer);
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

        DialogsApp* dialogs = furi_record_open(RECORD_DIALOGS);
        DialogMessage* message = dialog_message_alloc();
        dialog_message_set_text(
            message,
            "Error!\nSubGHz add-on module failed to start\n\nCheck that the module is plugged in",
            0,
            0,
            AlignLeft,
            AlignTop);
        dialog_message_show(dialogs, message);
        dialog_message_free(message);
        furi_record_close(RECORD_DIALOGS);
        return 0;
    }    

    LoRaApp* app = lora_app_alloc();
    view_dispatcher_run(app->view_dispatcher);

    lora_app_free(app);

    furi_hal_spi_bus_handle_deinit(spi);
    spi->cs = &gpio_ext_pa4;

    // Typically when a pin is no longer in use, it is set to analog mode.
    furi_hal_gpio_init_simple(pin_led, GpioModeAnalog);

    return 0;
}
