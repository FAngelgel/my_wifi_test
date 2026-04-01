#include "hal/spi_example.h"

#include <esp_err.h>
#include <esp_log.h>
#include <driver/spi_master.h>
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

namespace
{
    constexpr const char *TAG = "spi_example";

    // ESP32-S3 typically supports SPI2_HOST (FSPI) and SPI3_HOST (HSPI).
    constexpr spi_host_device_t kHost = SPI2_HOST;

    // Standard ADS1299 wiring for ESP32-S3
    constexpr gpio_num_t kPinSclk = GPIO_NUM_12;
    constexpr gpio_num_t kPinMosi = GPIO_NUM_11;
    constexpr gpio_num_t kPinMiso = GPIO_NUM_13;
    constexpr gpio_num_t kPinCs = GPIO_NUM_10;

    // Handles for the hardware and the safety lock
    spi_device_handle_t g_dev = nullptr;
    SemaphoreHandle_t g_spi_mutex = nullptr;

} // namespace

bool spi_example_init()
{
    // Prevent double-initialization
    if (g_dev != nullptr)
    {
        return true;
    }

    // 1. Create Mutex for thread safety
    g_spi_mutex = xSemaphoreCreateMutex();
    if (g_spi_mutex == nullptr)
    {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return false;
    }

    // 2. Configure the SPI Bus
    spi_bus_config_t buscfg = {};
    buscfg.mosi_io_num = kPinMosi;
    buscfg.miso_io_num = kPinMiso;
    buscfg.sclk_io_num = kPinSclk;
    buscfg.quadwp_io_num = -1;
    buscfg.quadhd_io_num = -1;
    buscfg.max_transfer_sz = 4096;

    esp_err_t ret = spi_bus_initialize(kHost, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE)
    {
        ESP_LOGE(TAG, "spi_bus_initialize failed: %s", esp_err_to_name(ret));
        return false;
    }

    // 3. Configure the ADS1299 Device on the bus
    spi_device_interface_config_t devcfg = {};
    devcfg.clock_speed_hz = 4 * 1000 * 1000; // 4MHz is stable for ADS1299
    devcfg.mode = 1;                         // Mode 1 (CPOL=0, CPHA=1)
    devcfg.spics_io_num = kPinCs;
    devcfg.queue_size = 7;
    devcfg.flags = SPI_DEVICE_NO_DUMMY;

    // ADDED FOR STABILITY: Gives the ADS1299 time to prepare after CS goes low
    devcfg.cs_ena_pretrans = 1;
    devcfg.cs_ena_posttrans = 1;

    ret = spi_bus_add_device(kHost, &devcfg, &g_dev);
    if (ret != ESP_OK)
    {
        ESP_LOGE(TAG, "spi_bus_add_device failed: %s", esp_err_to_name(ret));
        return false;
    }

    ESP_LOGI(TAG, "SPI initialized successfully for ADS1299");
    return true;
}

bool spi_example_transfer(const uint8_t *tx, uint8_t *rx, size_t len_bytes)
{
    if (g_dev == nullptr || g_spi_mutex == nullptr)
    {
        ESP_LOGE(TAG, "Driver not initialized");
        return false;
    }

    if (len_bytes == 0)
        return true;

    // 4. Protect the transfer with the Mutex
    if (xSemaphoreTake(g_spi_mutex, pdMS_TO_TICKS(100)) == pdTRUE)
    {
        spi_transaction_t t = {};
        t.length = len_bytes * 8;   // total bits to transmit
        t.rxlength = len_bytes * 8; // UPDATED: explicit bits to receive
        t.tx_buffer = tx;
        t.rx_buffer = rx;

        esp_err_t ret = spi_device_transmit(g_dev, &t);

        // 5. Always release the Mutex
        xSemaphoreGive(g_spi_mutex);

        if (ret != ESP_OK)
        {
            ESP_LOGE(TAG, "spi_device_transmit failed: %s", esp_err_to_name(ret));
            return false;
        }
        return true;
    }
    else
    {
        ESP_LOGE(TAG, "Failed to obtain SPI mutex (bus timeout)");
        return false;
    }
}

void spi_example_deinit()
{
    if (g_dev != nullptr)
    {
        spi_bus_remove_device(g_dev);
        g_dev = nullptr;
    }

    if (g_spi_mutex != nullptr)
    {
        vSemaphoreDelete(g_spi_mutex);
        g_spi_mutex = nullptr;
    }

    spi_bus_free(kHost);
}
