#pragma once

#include <stddef.h>
#include <stdint.h>

void device_info_init(void);
size_t device_info_build_payload(uint8_t *buffer, size_t capacity);
