#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/pio.h"
#include "hardware/dma.h"
#include "hardware/irq.h"
#include "hardware/clocks.h"
#include "i2s.pio.h"   // Your I2S PIO program header (from earlier)

// Audio buffer size (in samples, 16-bit)
#define AUDIO_BUFFER_SIZE 4096

// Ring buffer for audio samples (16-bit signed)
static int16_t audio_buffer[AUDIO_BUFFER_SIZE];
static volatile uint32_t write_pos = 0;
static volatile uint32_t read_pos = 0;

// PIO and DMA state (simplified)
PIO pio = pio0;
uint sm;
uint dma_chan;

void dma_handler() {
    // Clear the interrupt request.
    dma_hw->ints0 = 1u << dma_chan;

    // Update read_pos to indicate data consumed by DMA
    read_pos = (read_pos + AUDIO_BUFFER_SIZE) % AUDIO_BUFFER_SIZE;

    // Re-configure DMA to continue streaming from new read_pos
    dma_channel_set_read_addr(dma_chan, &audio_buffer[read_pos], true);
}

void setup_i2s() {
    // Load and configure PIO program for I2S here...
    // Assume i2s_program_init returns the state machine number
    sm = i2s_program_init(pio, 0, 18, 19, 20);  // BCLK, LRCLK, DIN pins

    // Setup DMA channel
    dma_chan = dma_claim_unused_channel(true);
    dma_channel_config c = dma_channel_get_default_config(dma_chan);
    channel_config_set_transfer_data_size(&c, DMA_SIZE_16);
    channel_config_set_read_increment(&c, true);
    channel_config_set_write_increment(&c, false);
    channel_config_set_dreq(&c, pio_get_dreq(pio, sm, true));
    channel_config_set_irq_quiet(&c, false);

    dma_channel_configure(
        dma_chan,
        &c,
        &pio->txf[sm],          // Write address (PIO TX FIFO)
        &audio_buffer[read_pos],// Read address (audio buffer)
        AUDIO_BUFFER_SIZE,      // Number of transfers
        false                   // Don't start yet
    );

    dma_channel_set_irq0_enabled(dma_chan, true);
    irq_set_exclusive_handler(DMA_IRQ_0, dma_handler);
    irq_set_enabled(DMA_IRQ_0, true);
}

int main() {
    stdio_usb_init();
    gpio_init(PICO_DEFAULT_LED_PIN);
    gpio_set_dir(PICO_DEFAULT_LED_PIN, GPIO_OUT);

    setup_i2s();

    dma_channel_start(dma_chan);

    while (!stdio_usb_connected()) {
        // wait for USB serial connection
        tight_loop_contents();
    }

    while (true) {
        // Read incoming audio samples from USB serial
        if (stdio_usb_readable()) {
            uint8_t buf[64];
            int bytes_read = stdio_usb_read(buf, sizeof(buf));
            if (bytes_read > 0) {
                // Copy incoming bytes to ring buffer as 16-bit samples
                for (int i = 0; i < bytes_read; i += 2) {
                    if ((write_pos + 1) % AUDIO_BUFFER_SIZE != read_pos) {
                        // Combine two bytes into int16 sample (little endian)
                        int16_t sample = buf[i] | (buf[i+1] << 8);
                        audio_buffer[write_pos] = sample;
                        write_pos = (write_pos + 1) % AUDIO_BUFFER_SIZE;
                    }
                    // else buffer full, drop samples or handle overflow here
                }
            }
        }
        // Blink LED for status
        gpio_put(PICO_DEFAULT_LED_PIN, 1);
        sleep_ms(100);
        gpio_put(PICO_DEFAULT_LED_PIN, 0);
        sleep_ms(100);
    }

    return 0;
}
