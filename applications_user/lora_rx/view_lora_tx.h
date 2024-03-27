#pragma oncerx

#include <stdint.h>
#include <gui/view.h>

typedef struct ViewLoRaTX ViewLoRaTX;

ViewLoRaTX* view_lora_tx_alloc();

void view_lora_tx_free(ViewLoRaTX* instance);

View* view_lora_tx_get_view(ViewLoRaTX* instance);