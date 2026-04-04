#pragma once

#include <cstddef>
#include <cstdint>

#ifdef __cplusplus
extern "C"
{
#endif
    // Minimal SPI master example (ESP-IDF spi_master).
    //
    // Usage:
    //   #include "spi/spi.h"
    //   spi_example_init();
    //   uint8_t tx[] = {0x9F, 0x00, 0x00, 0x00};
    //   uint8_t rx[sizeof(tx)] = {};
    //   spi_transfer(tx, rx, sizeof(tx));
    //   spi_deinit();

    bool spi_init();
    bool spi_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes);
    void spi_deinit();

#ifdef __cplusplus
}
#endif
