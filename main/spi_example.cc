#include "spi_example.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/spi_master.h>

namespace {

constexpr const char *TAG = "spi_example";

// Pick a general-purpose SPI host (ESP32-S3 typically supports SPI2_HOST/SPI3_HOST for user apps).
constexpr spi_host_device_t kHost = SPI2_HOST;

// Change these to match your wiring.
constexpr gpio_num_t kPinSclk = GPIO_NUM_12;
constexpr gpio_num_t kPinMosi = GPIO_NUM_11;
constexpr gpio_num_t kPinMiso = GPIO_NUM_13;
constexpr gpio_num_t kPinCs = GPIO_NUM_10;

spi_device_handle_t g_dev = nullptr;

} // namespace

bool spi_example_init()
{
    if (g_dev != nullptr)
    {
        return true;
    }

    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = kPinMosi;
    buscfg.miso_io_num = kPinMiso;
    buscfg.sclk_io_num = kPinSclk;
    buscfg.quadwp_io_num = GPIO_NUM_NC;
    buscfg.quadhd_io_num = GPIO_NUM_NC;
    buscfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(kHost, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }

    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 1 * 1000 * 1000; // start slow (1 MHz)
    devcfg.mode = 0;                         // SPI mode 0..3
    devcfg.spics_io_num = kPinCs;            // hardware CS
    devcfg.queue_size = 1;

    ret = spi_bus_add_device(kHost, &devcfg, &g_dev);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

bool spi_example_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes)
{
    if (g_dev == nullptr)
    {
        ESP_LOGE(TAG, "spi_example_transfer called before spi_example_init");
        return false;
    }
    if (len_bytes == 0)
    {
        return true;
    }

    spi_transaction_t t = {};
    t.length = len_bytes * 8; // in bits
    t.tx_buffer = tx;
    t.rx_buffer = rx;

    esp_err_t ret = spi_device_transmit(g_dev, &t);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(ret));
        return false;
    }

    return true;
}

void spi_example_deinit()
{
    if (g_dev != nullptr)
    {
        spi_bus_remove_device(g_dev);
        g_dev = nullptr;
    }

    // Freeing the bus is safe even if it was already initialized elsewhere,
    // but if your app shares the bus across devices, manage lifetime accordingly.
    spi_bus_free(kHost);
}

