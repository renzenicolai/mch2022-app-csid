#include "freertos/FreeRTOS.h"
#include "esp_system.h"
#include "driver/i2s.h"
#include "driver/rtc_io.h"
#include "driver/gpio.h"
#include "hal/gpio_hal.h"

#include "main.h"
#include "libcsid.h"

static pax_buf_t buf;
xQueueHandle buttonQueue;

#include <esp_log.h>
static const char *TAG = "csid";

#define I2S_NUM I2S_NUM_0

void disp_flush() {
    ili9341_write(get_ili9341(), buf.buf);
}

void exit_to_launcher() {
    REG_WRITE(RTC_CNTL_STORE0_REG, 0);
    esp_restart();
}

void audio_init(int sample_rate) {
    printf("%s: sample_rate=%d\n", __func__, sample_rate);

    // NOTE: buffer needs to be adjusted per AUDIO_SAMPLE_RATE
    i2s_config_t i2s_config = {
        .mode = I2S_MODE_MASTER | I2S_MODE_TX,
        .sample_rate = sample_rate,
        .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
        .channel_format = I2S_CHANNEL_FMT_RIGHT_LEFT,
        .communication_format = I2S_COMM_FORMAT_STAND_I2S,
        .dma_buf_count = 32,
        .dma_buf_len = 64,
        .intr_alloc_flags = 0,
        .use_apll = true
    };

    i2s_driver_install(I2S_NUM, &i2s_config, 0, NULL);
    i2s_pin_config_t pin_config = {
            .mck_io_num   = 0,
            .bck_io_num   = 4,//12,
            .ws_io_num    = 12,//4,
            .data_out_num = 13,
            .data_in_num  = I2S_PIN_NO_CHANGE
        };
    i2s_set_pin(I2S_NUM, &pin_config);
    
    PIN_FUNC_SELECT(PERIPHS_IO_MUX_GPIO0_U, FUNC_GPIO0_CLK_OUT1);
    WRITE_PERI_REG(PIN_CTRL, 0xFFF0);
    
    i2s_zero_dma_buffer(I2S_NUM);
}

void audio_terminate() {
    i2s_zero_dma_buffer(I2S_NUM);
    i2s_stop(I2S_NUM);

    i2s_start(I2S_NUM);


    esp_err_t err = rtc_gpio_init(GPIO_NUM_25);
    err = rtc_gpio_init(GPIO_NUM_26);
    if (err != ESP_OK)
    {
        abort();
    }

    err = rtc_gpio_set_direction(GPIO_NUM_25, RTC_GPIO_MODE_OUTPUT_ONLY);
    err = rtc_gpio_set_direction(GPIO_NUM_26, RTC_GPIO_MODE_OUTPUT_ONLY);
    if (err != ESP_OK)
    {
        abort();
    }

    err = rtc_gpio_set_level(GPIO_NUM_25, 0);
    err = rtc_gpio_set_level(GPIO_NUM_26, 0);
    if (err != ESP_OK)
    {
        abort();
    }

}

#define NUM_SAMPLES 300
unsigned short mono_samples_data[2 * NUM_SAMPLES];
unsigned short samples_data[2 * 2 * NUM_SAMPLES];

static void render_audio() {
    libcsid_render(mono_samples_data, NUM_SAMPLES);

    // Duplicate mono samples to create stereo buffer
    for(unsigned int i = 0; i < NUM_SAMPLES; i ++) {
        unsigned int sample_val = mono_samples_data[i];
        samples_data[i * 2 + 0] = (unsigned short) (((short)sample_val) * 0.7);
        samples_data[i * 2 + 1] = (unsigned short) (((short)sample_val) * 0.7);
    }

    int pos = 0;
    int left = 2 * 2 * NUM_SAMPLES;
    unsigned char *ptr = (unsigned char *)samples_data;

    while (left > 0) {
        size_t written = 0;
        i2s_write(I2S_NUM, (const char *)ptr, left, &written, 100 / portTICK_RATE_MS);
        pos += written;
        ptr += written;
        left -= written;
    }
}

#include "phantom.inc"

void app_main() {
    ESP_LOGI(TAG, "Welcome CSID app!");
    bsp_init();
    bsp_rp2040_init();
    buttonQueue = get_rp2040()->queue;
    pax_buf_init(&buf, NULL, 320, 240, PAX_BUF_16_565RGB);
    nvs_flash_init();
    wifi_init();
    
    audio_init(22050);
    
    libcsid_init(22050, SIDMODEL_6581);
    libcsid_load((unsigned char *)&phantom_of_the_opera_sid, phantom_of_the_opera_sid_len, 0);

    printf("SID Title: %s\n", libcsid_gettitle());
    printf("SID Author: %s\n", libcsid_getauthor());
    printf("SID Info: %s\n", libcsid_getinfo());
    
    pax_background(&buf, 0xFF0000);
    
    const pax_font_t *font = pax_font_saira_condensed;
    pax_draw_text(&buf, 0xffFFFFFF, font, font->default_size, 0, 0, "CSID");
    disp_flush();
    
    while (1) {
        render_audio();
    }
}
