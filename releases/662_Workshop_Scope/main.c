#include <string.h>
#include "pico/stdlib.h"
#include "pico/stdio_usb.h"
#include "pico/multicore.h"
#include "hardware/adc.h"
#include "hardware/dma.h"
#include "hardware/spi.h"
#include "hardware/pwm.h"

// Per Computer rev 1 docs: ADC0/GPIO26 and ADC1/GPIO27 are the audio jacks (Audio In 2 and 1),
// ADC2/GPIO28 is the knob mux (unused here), ADC3/GPIO29 is the CV mux. Mux select A (GPIO24)
// picks CV 1 (0) or CV 2 (1); B (GPIO25) only affects the knob side, so it stays low.
// Frame samples are interleaved ADC0, ADC1, ADC3. CV 1 and CV 2 alternate block by block, tagged in the header.
#define MUX_PIN_A 24
#define MUX_PIN_B 25
#define NUM_CH 3
#define SAMPLE_RATE_PER_CH 48000
#define BLOCK_SAMPLES 255 // multiple of NUM_CH
#define BLOCK_FRAMES (BLOCK_SAMPLES / NUM_CH)
#define CV_SETTLE_FRAMES 2 // frames after a mux switch that still carry the previous CV

// Outputs, with the same wiring and DAC word format as ComputerCard.
#define DAC_SCK 18
#define DAC_TX 19
#define DAC_CS 21
#define DAC_GAIN1X_ACTIVE 0x3000
#define DAC_CHANNEL_B 0x8000
#define CV_OUT_1 23
#define CV_OUT_2 22
#define CV_PWM_WRAP 1999 // 0 V is level 1000, +6 V is 0, -6 V is 2000

// Frame: 0xA5 0x5A, seq (u8), tag (u8: TAG_CV2 set when the CV sample is CV 2), then BLOCK_SAMPLES little-endian u16 values.
#define TAG_CV2 0x01
#define FRAME_BYTES (4 + BLOCK_SAMPLES * 2)

// Host command, one byte: 'P'/'p' pass-through on/off.
static volatile bool pass_through = true;

static uint16_t bufs[2][BLOCK_SAMPLES];
static uint8_t tags[2];
static int dma_ch[2];
static volatile uint8_t ready_mask = 0;
static uint8_t mux_a = 0;
static volatile uint32_t block_count = 0; // completed blocks; block k fills bufs[k & 1] and carries CV (k & 1) + 1

static void dma_isr(void)
{
    for (int i = 0; i < 2; i++)
    {
        if (dma_channel_get_irq0_status(dma_ch[i]))
        {
            dma_channel_acknowledge_irq0(dma_ch[i]);
            // Re-arm for the next chained trigger without starting it.
            dma_channel_set_write_addr(dma_ch[i], bufs[i], false);
            dma_channel_set_trans_count(dma_ch[i], BLOCK_SAMPLES, false);

            tags[i] = mux_a ? TAG_CV2 : 0;
            mux_a ^= 1;
            gpio_put(MUX_PIN_A, mux_a);
            block_count++;

            ready_mask |= 1u << i;
        }
    }
}

static void setup_dma(void)
{
    dma_ch[0] = dma_claim_unused_channel(true);
    dma_ch[1] = dma_claim_unused_channel(true);

    for (int i = 0; i < 2; i++)
    {
        dma_channel_config c = dma_channel_get_default_config(dma_ch[i]);
        channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
        channel_config_set_read_increment(&c, false);
        channel_config_set_write_increment(&c, true);
        channel_config_set_dreq(&c, DREQ_ADC);
        channel_config_set_chain_to(&c, dma_ch[1 - i]);
        dma_channel_configure(dma_ch[i], &c, bufs[i], &adc_hw->fifo, BLOCK_SAMPLES, false);
        dma_channel_set_irq0_enabled(dma_ch[i], true);
    }

    irq_set_exclusive_handler(DMA_IRQ_0, dma_isr);
    irq_set_enabled(DMA_IRQ_0, true);
}

static void setup_adc(void)
{
    gpio_init(MUX_PIN_A);
    gpio_set_dir(MUX_PIN_A, GPIO_OUT);
    gpio_put(MUX_PIN_A, 0);
    gpio_init(MUX_PIN_B);
    gpio_set_dir(MUX_PIN_B, GPIO_OUT);
    gpio_put(MUX_PIN_B, 0);

    adc_init();
    adc_gpio_init(26);
    adc_gpio_init(27);
    adc_gpio_init(29);
    adc_select_input(0);
    adc_set_round_robin((1u << 0) | (1u << 1) | (1u << 3));
    adc_fifo_setup(true, true, 1, false, false);
    adc_set_clkdiv((float)48000000 / (SAMPLE_RATE_PER_CH * NUM_CH) - 1);
}

static void setup_outputs(void)
{
    spi_init(spi0, 15625000);
    spi_set_format(spi0, 16, SPI_CPOL_0, SPI_CPHA_0, SPI_MSB_FIRST);
    gpio_set_function(DAC_SCK, GPIO_FUNC_SPI);
    gpio_set_function(DAC_TX, GPIO_FUNC_SPI);
    gpio_set_function(DAC_CS, GPIO_FUNC_SPI);

    gpio_set_function(CV_OUT_1, GPIO_FUNC_PWM);
    gpio_set_function(CV_OUT_2, GPIO_FUNC_PWM);
    pwm_config config = pwm_get_default_config();
    pwm_config_set_wrap(&config, CV_PWM_WRAP);
    pwm_init(pwm_gpio_to_slice_num(CV_OUT_1), &config, true);
    pwm_set_gpio_level(CV_OUT_1, 1000);
    pwm_set_gpio_level(CV_OUT_2, 1000);
}

// The ADC reads inverted (+6 V = 0) and the DAC and CV PWM outputs are inverted too, so raw codes pass straight through.
static inline void __not_in_flash_func(write_outputs)(uint32_t audio_out1, uint32_t audio_out2, uint32_t cv1, uint32_t cv2)
{
    uint16_t words[2] = {
        (uint16_t)(DAC_GAIN1X_ACTIVE | (audio_out1 & 0xFFF)),
        (uint16_t)(DAC_CHANNEL_B | DAC_GAIN1X_ACTIVE | (audio_out2 & 0xFFF)),
    };
    spi_write16_blocking(spi0, words, 2);
    pwm_set_gpio_level(CV_OUT_1, (cv1 * 125) >> 8);
    pwm_set_gpio_level(CV_OUT_2, (cv2 * 125) >> 8);
}

static inline uint32_t __not_in_flash_func(frames_written)(uint32_t k)
{
    uint32_t base = (uint32_t)bufs[k & 1];
    uint32_t write = dma_channel_hw_addr(dma_ch[k & 1])->write_addr;
    uint32_t samples = (write - base) >> 1;
    return samples >= BLOCK_SAMPLES ? BLOCK_FRAMES : samples / NUM_CH;
}

// Core 1 follows the ADC DMA frame by frame, so the outputs lag the inputs by about one 48 kHz frame.
static void __not_in_flash_func(core1_main)(void)
{
    uint32_t audio1 = 2048, audio2 = 2048, cv[2] = {2048, 2048};
    uint32_t emit_k = 0, emit_f = 0;

    while (true)
    {
        uint32_t k = block_count;
        if (k - emit_k > 1)
        {
            emit_k = k;
            emit_f = 0;
        }

        bool finished = emit_k < k;
        uint32_t avail = finished ? BLOCK_FRAMES : frames_written(emit_k);
        const uint16_t *buf = bufs[emit_k & 1];

        while (emit_f < avail)
        {
            const uint16_t *s = &buf[emit_f * NUM_CH];
            audio2 = s[0];
            audio1 = s[1];
            if (emit_f >= CV_SETTLE_FRAMES)
            {
                cv[emit_k & 1] = s[2];
            }
            emit_f++;

            if (pass_through)
            {
                write_outputs(audio1, audio2, cv[0], cv[1]);
            }
            else
            {
                write_outputs(2048, 2048, 2048, 2048);
            }
        }

        if (finished)
        {
            emit_k++;
            emit_f = 0;
        }
    }
}

int main(void)
{
    stdio_init_all();
    setup_adc();
    setup_outputs();
    setup_dma();

    dma_channel_start(dma_ch[0]);
    adc_run(true);
    multicore_launch_core1(core1_main);

    uint8_t seq = 0;
    uint8_t frame[FRAME_BYTES];
    frame[0] = 0xA5;
    frame[1] = 0x5A;

    while (true)
    {
        int key = getchar_timeout_us(0);
        if (key == 'P' || key == 'p')
        {
            pass_through = key == 'P';
        }

        for (int i = 0; i < 2; i++)
        {
            if (!(ready_mask & (1u << i)))
            {
                continue;
            }
            ready_mask &= ~(1u << i);

            if (!stdio_usb_connected())
            {
                continue;
            }

            frame[2] = seq++;
            frame[3] = tags[i];
            memcpy(&frame[4], bufs[i], BLOCK_SAMPLES * 2);
            stdio_put_string((const char *)frame, FRAME_BYTES, false, false);
        }
    }
}
