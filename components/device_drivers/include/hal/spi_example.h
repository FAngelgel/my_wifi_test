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
    //   #include "hal/spi_example.h"
    //   spi_example_init();
    //   uint8_t tx[] = {0x9F, 0x00, 0x00, 0x00};
    //   uint8_t rx[sizeof(tx)] = {};
    //   spi_example_transfer(tx, rx, sizeof(tx));
    //   spi_example_deinit();

    bool spi_example_init();
    bool spi_example_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes);
    void spi_example_deinit();

#ifdef __cplusplus
}
#endif
