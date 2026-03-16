//
// Copyright(C) 2026
//
// Experimental Tufty/GitHub badge video backend.
//

#include "config.h"

#include <stdint.h>
#include <string.h>

#include "pico/sem.h"
#include "hardware/gpio.h"
#include "pico/multicore.h"

#include "doomtype.h"
#include "i_endoom.h"
#include "i_input.h"
#include "i_video.h"
#include "m_config.h"
#include "tables.h"
#include "v_video.h"
#include "w_wad.h"
#include "z_zone.h"

#define BADGER_LOGICAL_WIDTH 160
#define BADGER_LOGICAL_HEIGHT 120

uint8_t __aligned(4) frame_buffer[2][SCREENWIDTH * MAIN_VIEWHEIGHT];
volatile uint8_t interp_in_use;
volatile uint8_t wipe_min;
semaphore_t render_frame_ready;
semaphore_t display_frame_freed;

should_be_const constcharstar video_driver = "";
should_be_const constcharstar window_position = "center";

boolean screenvisible = true;
boolean screensaver_mode = false;
isb_int8_t usegamma = 0;

unsigned int joywait = 0;

int screen_width = BADGER_LOGICAL_WIDTH;
int screen_height = BADGER_LOGICAL_HEIGHT;
int fullscreen = true;
int aspect_ratio_correct = false;
int integer_scaling = true;
int vga_porch_flash = false;
int force_software_renderer = false;

static pixel_t screenbuffer[SCREENWIDTH * SCREENHEIGHT];
static uint16_t badger_framebuffer[BADGER_LOGICAL_WIDTH * BADGER_LOGICAL_HEIGHT];
static uint16_t rgb565_palette[256];
static uint8_t palette_rgb[256][3];
static int next_pal = 0;
pixel_t *I_VideoBuffer = screenbuffer;

uint8_t next_video_type = VIDEO_TYPE_SINGLE;
uint8_t next_frame_index;
uint8_t next_overlay_index;
int16_t *wipe_yoffsets_raw;
uint8_t *wipe_yoffsets;
uint32_t *wipe_linelookup;

void __attribute__((weak)) badger_lcd_present_565(const uint16_t *framebuffer,
                                                  int width,
                                                  int height)
{
    (void) framebuffer;
    (void) width;
    (void) height;
}

static uint16_t rgb888_to_rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    return (uint16_t)(((r & 0xf8u) << 8u) | ((g & 0xfcu) << 3u) | (b >> 3u));
}

static void update_palette(void)
{
    static const uint8_t *playpal;
    static boolean calculate_palettes;

    if (next_pal < 0)
    {
        return;
    }

    if (playpal == NULL)
    {
        lumpindex_t lump = W_GetNumForName("PLAYPAL");
        playpal = W_CacheLumpNum(lump, PU_STATIC);
        calculate_palettes = W_LumpLength(lump) == 768;
    }

    if (!calculate_palettes || next_pal == 0)
    {
        const uint8_t *doompalette = playpal + next_pal * 768;

        for (int i = 0; i < 256; ++i)
        {
            int r = *doompalette++;
            int g = *doompalette++;
            int b = *doompalette++;

            if (usegamma)
            {
                r = gammatable[usegamma - 1][r];
                g = gammatable[usegamma - 1][g];
                b = gammatable[usegamma - 1][b];
            }

            palette_rgb[i][0] = (uint8_t) r;
            palette_rgb[i][1] = (uint8_t) g;
            palette_rgb[i][2] = (uint8_t) b;
            rgb565_palette[i] = rgb888_to_rgb565((uint8_t) r, (uint8_t) g, (uint8_t) b);
        }
    }
    else
    {
        int mul;
        int r0;
        int g0;
        int b0;
        const uint8_t *doompalette = playpal;

        if (next_pal < 9)
        {
            mul = next_pal * 65536 / 9;
            r0 = 255;
            g0 = 0;
            b0 = 0;
        }
        else if (next_pal < 13)
        {
            mul = (next_pal - 8) * 65536 / 8;
            r0 = 215;
            g0 = 186;
            b0 = 69;
        }
        else
        {
            mul = 65536 / 8;
            r0 = 0;
            g0 = 256;
            b0 = 0;
        }

        for (int i = 0; i < 256; ++i)
        {
            int r = *doompalette++;
            int g = *doompalette++;
            int b = *doompalette++;

            r += ((r0 - r) * mul) >> 16;
            g += ((g0 - g) * mul) >> 16;
            b += ((b0 - b) * mul) >> 16;

            palette_rgb[i][0] = (uint8_t) r;
            palette_rgb[i][1] = (uint8_t) g;
            palette_rgb[i][2] = (uint8_t) b;
            rgb565_palette[i] = rgb888_to_rgb565((uint8_t) r, (uint8_t) g, (uint8_t) b);
        }
    }

    next_pal = -1;
}

static void compose_display_buffer(void)
{
    const size_t top_bytes = SCREENWIDTH * MAIN_VIEWHEIGHT * sizeof(*screenbuffer);
    const size_t bottom_bytes = SCREENWIDTH * (SCREENHEIGHT - MAIN_VIEWHEIGHT) * sizeof(*screenbuffer);

    switch (next_video_type)
    {
        case VIDEO_TYPE_NONE:
        case VIDEO_TYPE_TEXT:
            memset(screenbuffer, 0, sizeof(screenbuffer));
            break;

        case VIDEO_TYPE_DOUBLE:
            memcpy(screenbuffer, frame_buffer[next_frame_index & 1], top_bytes);
            memset(screenbuffer + MAIN_VIEWHEIGHT * SCREENWIDTH, 0, bottom_bytes);
            break;

        case VIDEO_TYPE_SAVING:
        case VIDEO_TYPE_SINGLE:
        case VIDEO_TYPE_WIPE:
            memcpy(screenbuffer, frame_buffer[next_frame_index & 1], top_bytes);
            memcpy(screenbuffer + MAIN_VIEWHEIGHT * SCREENWIDTH,
                   frame_buffer[(next_frame_index ^ 1) & 1] + (MAIN_VIEWHEIGHT - 32) * SCREENWIDTH,
                   bottom_bytes);
            break;
    }

    if (next_video_type >= FIRST_VIDEO_TYPE_WITH_OVERLAYS)
    {
        pixel_t *saved_video_buffer = I_VideoBuffer;
        uint8_t saved_clip_top = vpatch_clip_top;
        uint8_t saved_clip_bottom = vpatch_clip_bottom;

        I_VideoBuffer = screenbuffer;
        V_RestoreBuffer();
        vpatch_clip_top = 0;
        vpatch_clip_bottom = SCREENHEIGHT;
        V_DrawPatchList(vpatchlists->overlays[next_overlay_index]);
        vpatch_clip_top = saved_clip_top;
        vpatch_clip_bottom = saved_clip_bottom;

        I_VideoBuffer = saved_video_buffer;
        V_RestoreBuffer();
    }
}

static void service_render_frame(void)
{
    if (sem_available(&render_frame_ready))
    {
        sem_acquire_blocking(&render_frame_ready);

        // Present the frame to the LCD
        update_palette();
        compose_display_buffer();
        for (int y = 0; y < BADGER_LOGICAL_HEIGHT; ++y)
        {
            int src_y = (y * (SCREENHEIGHT - 1)) / (BADGER_LOGICAL_HEIGHT - 1);
            for (int x = 0; x < BADGER_LOGICAL_WIDTH; ++x)
            {
                int src_x = x * 2;
                uint8_t pixel = screenbuffer[src_y * SCREENWIDTH + src_x];
                badger_framebuffer[y * BADGER_LOGICAL_WIDTH + x] =
                        rgb565_palette[pixel];
            }
        }
        badger_lcd_present_565(badger_framebuffer, BADGER_LOGICAL_WIDTH, BADGER_LOGICAL_HEIGHT);

        // Advance the wipe progress counter. In the VGA build this is updated
        // by the scanline renderer as it tracks the melt effect. Without this,
        // the wipe state machine in pd_end_frame() never advances past
        // WIPESTATE_SKIP1, causing an infinite loop in D_RunFrame().
        if (wipe_min < 200)
            wipe_min += 8;

        sem_release(&display_frame_freed);
    }
}

extern void pd_init(void);
extern void pd_core1_loop(void);

static semaphore_t core1_launch;

static void badge_core1(void)
{
    sem_release(&core1_launch);
    while (true)
    {
        pd_core1_loop();
    }
}

void I_InitGraphics(void)
{
    memset(screenbuffer, 0, sizeof(screenbuffer));
    memset(frame_buffer, 0, sizeof(frame_buffer));
    memset(badger_framebuffer, 0, sizeof(badger_framebuffer));
    I_VideoBuffer = screenbuffer;
    next_pal = 0;
    interp_in_use = 0;
    wipe_min = 0;
    sem_init(&render_frame_ready, 0, 2);
    sem_init(&display_frame_freed, 1, 2);
    pd_init();
    sem_init(&core1_launch, 0, 1);
    multicore_launch_core1(badge_core1);
    sem_acquire_blocking(&core1_launch);
#if USE_ZONE_FOR_MALLOC
    extern boolean disallow_core1_malloc;
    disallow_core1_malloc = true;
#endif
    update_palette();
}

void I_ShutdownGraphics(void)
{
}

void I_GraphicsCheckCommandLine(void)
{
}

void I_SetPaletteNum(int doompalette)
{
    next_pal = doompalette;
}

int I_GetPaletteIndex(int r, int g, int b)
{
    int best = 0;
    int best_diff = INT32_MAX;

    for (int i = 0; i < 256; ++i)
    {
        int dr = r - palette_rgb[i][0];
        int dg = g - palette_rgb[i][1];
        int db = b - palette_rgb[i][2];
        int diff = dr * dr + dg * dg + db * db;

        if (diff < best_diff)
        {
            best_diff = diff;
            best = i;
        }
    }

    return best;
}

void I_UpdateNoBlit(void)
{
    service_render_frame();
}

void I_FinishUpdate(void)
{
    service_render_frame();
}

void I_ReadScreen(pixel_t *scr)
{
    memcpy(scr, I_VideoBuffer, SCREENWIDTH * SCREENHEIGHT * sizeof(*scr));
}

void I_BeginRead(void)
{
}

void I_SetWindowTitle(const char *title)
{
    (void) title;
}

void I_CheckIsScreensaver(void)
{
}

void I_SetGrabMouseCallback(grabmouse_callback_t func)
{
    (void) func;
}

void I_DisplayFPSDots(boolean dots_on)
{
    (void) dots_on;
}

void I_BindVideoVariables(void)
{
    M_BindIntVariable("fullscreen",                &fullscreen);
    M_BindIntVariable("aspect_ratio_correct",      &aspect_ratio_correct);
    M_BindIntVariable("integer_scaling",           &integer_scaling);
    M_BindIntVariable("vga_porch_flash",           &vga_porch_flash);
    M_BindIntVariable("force_software_renderer",   &force_software_renderer);
    M_BindStringVariable("video_driver",           &video_driver);
    M_BindStringVariable("window_position",        &window_position);
}

void I_InitWindowTitle(void)
{
}

void I_InitWindowIcon(void)
{
}

void I_StartFrame(void)
{
    service_render_frame();
}

void I_StartTic(void)
{
    I_GetEvent();
}

void I_EnableLoadingDisk(int xoffs, int yoffs)
{
    (void) xoffs;
    (void) yoffs;
}

void I_GetWindowPosition(int *x, int *y, int w, int h)
{
    (void) w;
    (void) h;
    *x = 0;
    *y = 0;
}

void I_Endoom(should_be_const byte *endoom_data)
{
    (void) endoom_data;
    next_video_type = VIDEO_TYPE_TEXT;
    memset(badger_framebuffer, 0, sizeof(badger_framebuffer));
    badger_lcd_present_565(badger_framebuffer, BADGER_LOGICAL_WIDTH, BADGER_LOGICAL_HEIGHT);
}
