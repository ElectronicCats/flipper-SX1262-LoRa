#include <furi.h>
#include <gui/gui.h>
#include <storage/storage.h>

#define PATHAPP "apps_data/lora"
#define PATHAPPEXT EXT_PATH(PATHAPP)
#define PATHLORA PATHAPPEXT "/data.txt"

typedef struct {
    int type;
    InputEvent input;
} AppEvent;

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

bool write_text_to_file(File* file, const char* text) {

    FURI_LOG_I("FileSys", "in write_text_to_file...");

    // Abre o crea el archivo para escritura
    if(storage_file_open(file, PATHLORA, FSAM_WRITE, FSOM_CREATE_ALWAYS)) {

        FURI_LOG_I("FileSys", "open file for write...");

        // Escribe el texto en el archivo
        size_t bytes_written = storage_file_write(file, text, strlen(text));
        // Cierra el archivo
        storage_file_close(file);
        return bytes_written == strlen(text);
    } else {
        FURI_LOG_E("FileSys", "ABANDONE: Failed to open file for writing");
        return false;
    }
}

bool read_text_from_file(File* file, char* buffer, size_t buffer_size) {
    // Abre el archivo para lectura
    if(storage_file_open(file, PATHLORA, FSAM_READ, FSOM_OPEN_EXISTING)) {
        // Lee el contenido del archivo
        size_t bytes_read = storage_file_read(file, buffer, buffer_size - 1);
        buffer[bytes_read] = '\0'; // Asegura que la cadena esté terminada correctamente
        // Cierra el archivo
        storage_file_close(file);
        return true;
    } else {
        FURI_LOG_E("FileSys", "Failed to open file for reading");
        return false;
    }
}

//+++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++++

static void input_callback(InputEvent* input_event, void* ctx) {
    FuriMessageQueue* queue = ctx;
    AppEvent event = {
        .type = input_event->type,
        .input = *input_event,
    };
    furi_message_queue_put(queue, &event, FuriWaitForever);
}
static void render_callback(Canvas* canvas, void* ctx) {
    UNUSED(ctx);
    canvas_set_font(canvas, FontPrimary);
    canvas_draw_str_aligned(canvas, 15, 30, AlignLeft, AlignTop, "HELLO WORLD!");
}
//static ViewPort* view_port = view_port_alloc();
int32_t hello_storage_demo_app(void* p) {
    UNUSED(p);
    FuriMessageQueue* queue = furi_message_queue_alloc(8, sizeof(AppEvent));
    ViewPort* view_port = view_port_alloc();
    view_port_input_callback_set(view_port, input_callback, queue);
    view_port_draw_callback_set(view_port, render_callback, NULL);
    Gui* gui = furi_record_open(RECORD_GUI);
    gui_add_view_port(gui, view_port, GuiLayerFullscreen);

    Storage* storage;
    char read_buffer[1024];
    storage = furi_record_open(RECORD_STORAGE);
    File* file = storage_file_alloc(storage);

    // Escritura en archivo
    if(write_text_to_file(file, "Hello, Flipper!")) {
        FURI_LOG_I("FileSys", "File written successfully");
    } else {
        FURI_LOG_E("FileSys", "Failed to write file");
    }

    // Lectura de archivo
    if(read_text_from_file(file, read_buffer, sizeof(read_buffer))) {
        FURI_LOG_I("FileSys", "File read successfully: %s", read_buffer);
    } else {
        FURI_LOG_E("FileSys", "Failed to read file");
    }

    storage_file_free(file);

    bool processing = true;
    AppEvent event;
    do {
        if(furi_message_queue_get(queue, &event, FuriWaitForever) == FuriStatusOk) {
            if(event.input.type == InputTypeShort && event.input.key == InputKeyBack) {
                processing = false;
            }
        }
    } while(processing);
    view_port_enabled_set(view_port, false);
    gui_remove_view_port(gui, view_port);
    view_port_free(view_port);
    furi_record_close(RECORD_GUI);
    furi_message_queue_free(queue);
    return 0;
}
