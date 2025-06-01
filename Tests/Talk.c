#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"  // We will write this PIO program inline below

void i2s_program_init(PIO pio, uint sm, uint offset, uint pin_bclk, uint pin_lrclk, uint pin_data) {
    // Configure pins
    pio_gpio_init(pio, pin_bclk);
    pio_gpio_init(pio, pin_lrclk);
    pio_gpio_init(pio, pin_data);

    // Set pin directions
    pio_sm_set_consecutive_pindirs(pio, sm, pin_data, 1, true); // data out
    pio_sm_set_consecutive_pindirs(pio, sm, pin_bclk, 1, true); // BCLK out
    pio_sm_set_consecutive_pindirs(pio, sm, pin_lrclk, 1, true); // LRCLK out

    // Configure state machine
    pio_sm_config c = i2s_program_get_default_config(offset);

    // Set pins for sideset (BCLK)
    sm_config_set_sideset_pins(&c, pin_bclk);

    // Shift out MSB first
    sm_config_set_out_shift(&c, false, true, 32);

    // Set pin for out (data)
    sm_config_set_out_pins(&c, pin_data, 1);

    // Set FIFO join so TX FIFO is 32-bit wide
    sm_config_set_fifo_join(&c, PIO_FIFO_JOIN_TX);

    // Set clock div so sample rate matches
    // The BCLK frequency is 64 * sample_rate (for 32 bits stereo 16-bit)
    // So clock divider = system_clock / (64 * sample_rate)
    float div = (float)clock_get_hz(clk_sys) / (64 * SAMPLE_RATE);
    sm_config_set_clkdiv(&c, div);

    // Initialize SM
    pio_sm_init(pio, sm, offset, &c);

    // Enable SM
    pio_sm_set_enabled(pio, sm, true);
}

// I2S pins
#define PIN_BCLK 18
#define PIN_LRCLK 19
#define PIN_DATA 20

// Audio sample rate
#define SAMPLE_RATE 44100

// Audio frequency for test tone (Hz)
#define TONE_FREQ 440

// Number of samples in sine wave table
#define WAVE_SAMPLES 32

// Sine wave table (16-bit samples, signed)
static int16_t sine_wave[WAVE_SAMPLES];

// Function to fill sine wave table
void fill_sine_wave() {
    for (int i = 0; i < WAVE_SAMPLES; i++) {
        // Generate sine wave between -32767 and 32767
        sine_wave[i] = (int16_t)(32767 * sinf(2 * 3.1415926f * i / WAVE_SAMPLES));
    }
}

// Main program
int main() {
    stdio_init_all();

    printf("Starting I2S test tone...\n");

    fill_sine_wave();

    // Initialize PIO and load i2s program
    PIO pio = pio0;
    uint sm = 0;
    uint offset = pio_add_program(pio, &i2s_program);

    i2s_program_init(pio, sm, offset, PIN_BCLK, PIN_LRCLK, PIN_DATA);

    int sample_index = 0;
    int samples_per_cycle = SAMPLE_RATE / TONE_FREQ;

    while (1) {
        // Wait if FIFO full
        while (pio_sm_is_tx_fifo_full(pio, sm));

        // Get next sample (repeat samples to match tone frequency)
        int16_t sample = sine_wave[sample_index * WAVE_SAMPLES / samples_per_cycle % WAVE_SAMPLES];

        // I2S 16-bit stereo sample format: combine left and right (same sample)
        // Pack left sample and right sample into 32-bit value:
        // Left sample (upper 16 bits), Right sample (lower 16 bits)
        uint32_t i2s_sample = ((uint16_t)sample << 16) | ((uint16_t)sample & 0xFFFF);

        // Push to TX FIFO
        pio_sm_put(pio, sm, i2s_sample);

        sample_index = (sample_index + 1) % samples_per_cycle;
    }

    return 0;
}
