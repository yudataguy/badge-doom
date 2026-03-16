//
// Copyright(C) 2026
//
// Experimental native Tufty/GitHub badge hardware bridge.
//

#include "config.h"

#include <stdbool.h>
#include <stdint.h>

#include "pico/stdlib.h"
#include "hardware/clocks.h"
#include "hardware/dma.h"
#include "hardware/gpio.h"
#include "hardware/pio.h"
#include "st7789_parallel.pio.h"

#define BADGER_BUTTON_UP_BIT    (1u << 0)
#define BADGER_BUTTON_DOWN_BIT  (1u << 1)
#define BADGER_BUTTON_A_BIT     (1u << 2)
#define BADGER_BUTTON_B_BIT     (1u << 3)
#define BADGER_BUTTON_C_BIT     (1u << 4)
#define BADGER_BUTTON_HOME_BIT  (1u << 5)

#define TUFTY_LCD_BACKLIGHT_PIN 26
#define TUFTY_LCD_CS_PIN        27
#define TUFTY_LCD_DC_PIN        28
#define TUFTY_LCD_WR_PIN        30
#define TUFTY_LCD_RD_PIN        31
#define TUFTY_LCD_D0_PIN        32

#define TUFTY_BUTTON_DOWN_PIN   6
#define TUFTY_BUTTON_A_PIN      7
#define TUFTY_BUTTON_B_PIN      8
#define TUFTY_BUTTON_C_PIN      9
#define TUFTY_BUTTON_UP_PIN     10
#define TUFTY_BUTTON_HOME_PIN   22

enum st7789_reg
{
    ST7789_SWRESET  = 0x01,
    ST7789_TEOFF    = 0x34,
    ST7789_TEON     = 0x35,
    ST7789_STE      = 0x44,
    ST7789_MADCTL   = 0x36,
    ST7789_COLMOD   = 0x3A,
    ST7789_RAMCTRL  = 0xB0,
    ST7789_GCTRL    = 0xB7,
    ST7789_VCOMS    = 0xBB,
    ST7789_LCMCTRL  = 0xC0,
    ST7789_VDVVRHEN = 0xC2,
    ST7789_VRHS     = 0xC3,
    ST7789_VDVS     = 0xC4,
    ST7789_FRCTRL2  = 0xC6,
    ST7789_PWCTRL1  = 0xD0,
    ST7789_PORCTRL  = 0xB2,
    ST7789_GMCTRP1  = 0xE0,
    ST7789_GMCTRN1  = 0xE1,
    ST7789_INVOFF   = 0x20,
    ST7789_SLPOUT   = 0x11,
    ST7789_DISPON   = 0x29,
    ST7789_RAMWR    = 0x2C,
    ST7789_INVON    = 0x21,
    ST7789_CASET    = 0x2A,
    ST7789_RASET    = 0x2B,
};

static bool tufty_buttons_initialized;
static bool tufty_display_initialized;
static PIO tufty_lcd_pio = pio1;
static uint tufty_lcd_sm;
static uint tufty_lcd_offset;
static int tufty_lcd_dma_channel;
static uint32_t tufty_lcd_startup_hz;
static uint16_t tufty_linebuffer[240 * 4];

static inline void pio_sm_block_until_stalled(PIO pio, uint sm)
{
    uint32_t sm_stall_mask = 1u << (sm + PIO_FDEBUG_TXSTALL_LSB);
    pio->fdebug = sm_stall_mask;

    while ((pio->fdebug & sm_stall_mask) == 0)
    {
    }
}

static void tufty_configure_dma(bool enable_read_increment)
{
    dma_channel_config config = dma_channel_get_default_config(tufty_lcd_dma_channel);
    channel_config_set_read_increment(&config, enable_read_increment);
    channel_config_set_transfer_data_size(&config, DMA_SIZE_8);
    channel_config_set_bswap(&config, false);
    channel_config_set_dreq(&config, pio_get_dreq(tufty_lcd_pio, tufty_lcd_sm, true));
    dma_channel_configure(tufty_lcd_dma_channel,
                          &config,
                          &tufty_lcd_pio->txf[tufty_lcd_sm],
                          NULL,
                          0,
                          false);
}

static inline void tufty_wait_for_dma(void)
{
    dma_channel_wait_for_finish_blocking(tufty_lcd_dma_channel);
    pio_sm_block_until_stalled(tufty_lcd_pio, tufty_lcd_sm);
}

static void tufty_write_blocking(const uint8_t *src, size_t len)
{
    dma_channel_set_trans_count(tufty_lcd_dma_channel, len, false);
    dma_channel_set_read_addr(tufty_lcd_dma_channel, src, true);
    tufty_wait_for_dma();
}

static void tufty_start_dma(const uint8_t *src, size_t len)
{
    dma_channel_set_trans_count(tufty_lcd_dma_channel, len, false);
    dma_channel_set_read_addr(tufty_lcd_dma_channel, src, true);
}

static void tufty_command(uint8_t command, const uint8_t *data, size_t len)
{
    tufty_wait_for_dma();
    gpio_put(TUFTY_LCD_DC_PIN, 0);
    gpio_put(TUFTY_LCD_CS_PIN, 0);
    tufty_write_blocking(&command, 1);

    if (data != NULL && len != 0)
    {
        gpio_put(TUFTY_LCD_DC_PIN, 1);
        tufty_write_blocking(data, len);
    }

    gpio_put(TUFTY_LCD_CS_PIN, 1);
}

static void tufty_set_data_mode(void)
{
    gpio_put(TUFTY_LCD_DC_PIN, 1);
    gpio_put(TUFTY_LCD_CS_PIN, 0);
}

static void tufty_gpio_init_out(uint pin, bool initial_value)
{
    gpio_init(pin);
    gpio_set_dir(pin, GPIO_OUT);
    gpio_put(pin, initial_value);
}

#define TUFTY_POWER_EN_PIN      41

static void tufty_init_display(void)
{
    // Enable the badge peripheral power rail before any LCD communication.
    gpio_init(TUFTY_POWER_EN_PIN);
    gpio_set_dir(TUFTY_POWER_EN_PIN, GPIO_OUT);
    gpio_put(TUFTY_POWER_EN_PIN, 1);
    sleep_ms(50);

    static const uint8_t porctrl[]  = {0x0c, 0x0c, 0x00, 0x33, 0x33};
    static const uint8_t colmod[]   = {0x05};
    static const uint8_t lcmctrl[]  = {0x2c};
    static const uint8_t vdvvrhen[] = {0x01};
    static const uint8_t vrhs[]     = {0x0f};
    static const uint8_t vdvs[]     = {0x20};
    static const uint8_t pwctrl1[]  = {0xa4, 0xa1};
    static const uint8_t frctrl2[]  = {0x0f};
    static const uint8_t ramctrl[]  = {0x00, 0xc0};
    static const uint8_t gctrl[]    = {0x35};
    static const uint8_t vcoms[]    = {0x1b};
    static const uint8_t gmctrp1[]  = {0xF0, 0x00, 0x06, 0x04, 0x05, 0x05, 0x31, 0x44, 0x48, 0x36, 0x12, 0x12, 0x2B, 0x34};
    static const uint8_t gmctrn1[]  = {0xF0, 0x0B, 0x0F, 0x0F, 0x0D, 0x26, 0x31, 0x43, 0x47, 0x38, 0x14, 0x14, 0x2C, 0x32};
    static const uint8_t teon[]     = {0x00};
    static const uint8_t ste[]      = {0x00, 0x00};
    static const uint8_t madctl[]   = {0x90};
    static const uint8_t caset[]    = {0x00, 0x00, 0x00, 0xef};
    static const uint8_t raset[]    = {0x00, 0x00, 0x01, 0x3f};

    tufty_gpio_init_out(TUFTY_LCD_CS_PIN, 1);
    tufty_gpio_init_out(TUFTY_LCD_DC_PIN, 1);
    tufty_gpio_init_out(TUFTY_LCD_RD_PIN, 1);
    tufty_gpio_init_out(TUFTY_LCD_BACKLIGHT_PIN, 0);

    pio_set_gpio_base(tufty_lcd_pio, TUFTY_LCD_D0_PIN >= 32 ? 16 : 0);

    tufty_lcd_sm = pio_claim_unused_sm(tufty_lcd_pio, true);
    tufty_lcd_offset = pio_add_program(tufty_lcd_pio, &st7789_parallel_program);

    pio_gpio_init(tufty_lcd_pio, TUFTY_LCD_WR_PIN);
    gpio_set_function(TUFTY_LCD_RD_PIN, GPIO_FUNC_SIO);
    gpio_set_dir(TUFTY_LCD_RD_PIN, GPIO_OUT);

    for (uint pin = 0; pin < 8; ++pin)
    {
        pio_gpio_init(tufty_lcd_pio, TUFTY_LCD_D0_PIN + pin);
    }

    pio_sm_set_consecutive_pindirs(tufty_lcd_pio, tufty_lcd_sm, TUFTY_LCD_D0_PIN, 8, true);
    pio_sm_set_consecutive_pindirs(tufty_lcd_pio, tufty_lcd_sm, TUFTY_LCD_WR_PIN, 1, true);

    pio_sm_config pio_config = st7789_parallel_program_get_default_config(tufty_lcd_offset);
    // On RP2350B, sm_config_set_* takes absolute GPIO numbers (the SDK
    // stores upper bits in pinhi and handles the GPIO base internally).
    sm_config_set_out_pins(&pio_config, TUFTY_LCD_D0_PIN, 8);
    sm_config_set_sideset_pins(&pio_config, TUFTY_LCD_WR_PIN);
    sm_config_set_fifo_join(&pio_config, PIO_FIFO_JOIN_TX);
    sm_config_set_out_shift(&pio_config, false, true, 8);

    tufty_lcd_startup_hz = clock_get_hz(clk_sys);
    // Badge ST7789 max PIO clock = 44MHz. At 270MHz: ceil(270/44) = 7.
    uint clk_div = (tufty_lcd_startup_hz + 44000000 - 1) / 44000000;
    sm_config_set_clkdiv_int_frac(&pio_config, clk_div, 0);

    pio_sm_init(tufty_lcd_pio, tufty_lcd_sm, tufty_lcd_offset, &pio_config);
    pio_sm_set_enabled(tufty_lcd_pio, tufty_lcd_sm, true);

    tufty_lcd_dma_channel = dma_claim_unused_channel(true);
    tufty_configure_dma(true);

    tufty_command(ST7789_SWRESET, NULL, 0);
    sleep_ms(150);

    tufty_command(ST7789_COLMOD, colmod, sizeof(colmod));
    tufty_command(ST7789_PORCTRL, porctrl, sizeof(porctrl));
    tufty_command(ST7789_LCMCTRL, lcmctrl, sizeof(lcmctrl));
    tufty_command(ST7789_VDVVRHEN, vdvvrhen, sizeof(vdvvrhen));
    tufty_command(ST7789_VRHS, vrhs, sizeof(vrhs));
    tufty_command(ST7789_VDVS, vdvs, sizeof(vdvs));
    tufty_command(ST7789_PWCTRL1, pwctrl1, sizeof(pwctrl1));
    tufty_command(ST7789_FRCTRL2, frctrl2, sizeof(frctrl2));
    tufty_command(ST7789_RAMCTRL, ramctrl, sizeof(ramctrl));
    tufty_command(ST7789_GCTRL, gctrl, sizeof(gctrl));
    tufty_command(ST7789_VCOMS, vcoms, sizeof(vcoms));
    tufty_command(ST7789_GMCTRP1, gmctrp1, sizeof(gmctrp1));
    tufty_command(ST7789_GMCTRN1, gmctrn1, sizeof(gmctrn1));
    tufty_command(ST7789_INVON, NULL, 0);
    tufty_command(ST7789_SLPOUT, NULL, 0);
    sleep_ms(100);

    tufty_command(ST7789_CASET, caset, sizeof(caset));
    tufty_command(ST7789_RASET, raset, sizeof(raset));
    tufty_command(ST7789_MADCTL, madctl, sizeof(madctl));
    tufty_command(ST7789_TEOFF, NULL, 0);
    tufty_command(ST7789_TEON, teon, sizeof(teon));
    tufty_command(ST7789_STE, ste, sizeof(ste));
    tufty_command(ST7789_DISPON, NULL, 0);

    gpio_put(TUFTY_LCD_BACKLIGHT_PIN, 1);
}

static void tufty_init_buttons(void)
{
    static const uint button_pins[] = {
            TUFTY_BUTTON_A_PIN,
            TUFTY_BUTTON_B_PIN,
            TUFTY_BUTTON_C_PIN,
            TUFTY_BUTTON_UP_PIN,
            TUFTY_BUTTON_DOWN_PIN,
            TUFTY_BUTTON_HOME_PIN,
    };

    for (uint i = 0; i < sizeof(button_pins) / sizeof(button_pins[0]); ++i)
    {
        gpio_init(button_pins[i]);
        gpio_set_dir(button_pins[i], GPIO_IN);
        gpio_pull_up(button_pins[i]);
    }
}

static void tufty_buttons_init_once(void)
{
    if (tufty_buttons_initialized)
    {
        return;
    }

    tufty_init_buttons();
    tufty_buttons_initialized = true;
}

static void tufty_display_init_once(void)
{
    if (tufty_display_initialized)
    {
        return;
    }

    tufty_buttons_init_once();
    tufty_init_display();
    tufty_display_initialized = true;
}

uint32_t badger_read_buttons(void)
{
    uint32_t buttons = 0;

    tufty_buttons_init_once();

    buttons |= gpio_get(TUFTY_BUTTON_UP_PIN)   ? 0 : BADGER_BUTTON_UP_BIT;
    buttons |= gpio_get(TUFTY_BUTTON_DOWN_PIN) ? 0 : BADGER_BUTTON_DOWN_BIT;
    buttons |= gpio_get(TUFTY_BUTTON_A_PIN)    ? 0 : BADGER_BUTTON_A_BIT;
    buttons |= gpio_get(TUFTY_BUTTON_B_PIN)    ? 0 : BADGER_BUTTON_B_BIT;
    buttons |= gpio_get(TUFTY_BUTTON_C_PIN)    ? 0 : BADGER_BUTTON_C_BIT;
    buttons |= gpio_get(TUFTY_BUTTON_HOME_PIN) ? 0 : BADGER_BUTTON_HOME_BIT;

    return buttons;
}

void badger_lcd_present_565(const uint16_t *framebuffer, int width, int height)
{
    uint16_t *buf_a = tufty_linebuffer;
    uint16_t *buf_b = tufty_linebuffer + 240 * 2;

    tufty_display_init_once();

    if (framebuffer == NULL || width != 160 || height != 120)
    {
        return;
    }

    if (clock_get_hz(clk_sys) != tufty_lcd_startup_hz)
    {
        tufty_lcd_startup_hz = clock_get_hz(clk_sys);
        uint div = (tufty_lcd_startup_hz + 44000000 - 1) / 44000000;
        pio_sm_set_clkdiv_int_frac(tufty_lcd_pio, tufty_lcd_sm, div, 0);
    }

    // Send RAMWR command and switch to data mode WITHOUT deselecting CS.
    // The ST7789 requires CS to stay low from RAMWR through all pixel data.
    tufty_wait_for_dma();
    gpio_put(TUFTY_LCD_DC_PIN, 0);
    gpio_put(TUFTY_LCD_CS_PIN, 0);
    {
        uint8_t ramwr_cmd = ST7789_RAMWR;
        tufty_write_blocking(&ramwr_cmd, 1);
    }
    gpio_put(TUFTY_LCD_DC_PIN, 1);

    for (int x = 0; x < width; ++x)
    {
        for (int y = 0; y < height; ++y)
        {
            uint16_t pixel = __builtin_bswap16(framebuffer[y * width + x]);
            buf_a[y * 2] = pixel;
            buf_a[y * 2 + 1] = pixel;
            buf_a[(height + y) * 2] = pixel;
            buf_a[(height + y) * 2 + 1] = pixel;
        }

        tufty_wait_for_dma();
        tufty_start_dma((const uint8_t *) buf_a, 240 * 2 * 2);

        uint16_t *tmp = buf_a;
        buf_a = buf_b;
        buf_b = tmp;
    }

    tufty_wait_for_dma();
    gpio_put(TUFTY_LCD_CS_PIN, 1);
}
