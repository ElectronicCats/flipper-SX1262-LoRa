#pragma once

#include <gui/view.h>
#include <stdint.h>

typedef struct ViewLoRaRX ViewLoRaRX;

ViewLoRaRX* view_lora_rx_alloc();

void view_lora_rx_free(ViewLoRaRX* instance);

View* view_lora_rx_get_view(ViewLoRaRX* instance);
