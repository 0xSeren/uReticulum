#pragma once

#include <stdint.h>
#include <stddef.h>

namespace HeltecV3::Oled {

    constexpr int WIDTH  = 128;
    constexpr int HEIGHT = 64;
    constexpr int COLS   = WIDTH / 6;   /* 21 chars per row with 6x8 font */
    constexpr int ROWS   = HEIGHT / 8;  /* 8 rows of 8 pixels */

    /* Power on Vext, init I2C, reset the SSD1306, push init sequence.
     * Returns true on success. Safe to call multiple times. */
    bool init();

    /* Clear the framebuffer (call flush() to push to display). */
    void clear();

    /* Write a string at (row, col) in the framebuffer. Clips to screen
     * bounds. Does not wrap; text beyond col 20 is dropped. */
    void print(int row, int col, const char* text);

    /* Clear a single row and write new text into it. Convenience. */
    void set_line(int row, const char* text);

    /* Draw a horizontal line across a row position (y pixel, thin). */
    void hline(int y);

    /* Push the framebuffer to the SSD1306. */
    void flush();

    /* Power-save the panel. SSD1306 0xAE: display off, GDDRAM preserved.
     * Call suspend() before entering a period where APB clock transitions
     * (due to light sleep wake/sleep) could glitch the I2C bus. */
    void suspend();
    void resume();
    bool is_suspended();

}
