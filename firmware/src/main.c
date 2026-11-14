/* UV-98 Plus / RT-880G open firmware, test 10: receive FM/NFM/AM/USB, squelch, side keys, voice TX on
 * the amateur bands (power MIN by default, LOW/MID/HIGH from calibration), CTCSS/DCS on TX, 1750 Hz
 * burst on the alarm button, 60 s time-out timer, CTCSS/DCS tone squelch on receive (test 11).
 * Pin map and stock sequences: ../FINDINGS.md. BK4819 #1 is the radio; #2 is reset and put to sleep. */
#include "at32f423.h"
#include "fonts_aa.h"

#define FW_NAME  "UV98+ open test 16"
#define APP_BASE 0x08002800u

/* ---------------- timing ---------------- */
static volatile uint32_t ticks;
void SysTick_Handler(void) { ticks++; }

static void delay_ms(uint32_t ms)
{
    uint32_t t = ticks;
    while (ticks - t < ms) { }
}

static void delay_short(void)
{
    for (volatile int i = 0; i < 20; i++) { }
}

/* ---------------- GPIO helpers ---------------- */
static void pin_cfg(gpio_type *port, uint16_t pins, gpio_mode_type mode, gpio_pull_type pull)
{
    gpio_init_type g;
    gpio_default_para_init(&g);
    g.gpio_pins = pins;
    g.gpio_mode = mode;
    g.gpio_out_type = GPIO_OUTPUT_PUSH_PULL;
    g.gpio_pull = pull;
    g.gpio_drive_strength = GPIO_DRIVE_STRENGTH_STRONGER;
    gpio_init(port, &g);
}
#define pin_out(p, n)      pin_cfg(p, n, GPIO_MODE_OUTPUT, GPIO_PULL_NONE)
#define pin_in(p, n, pull) pin_cfg(p, n, GPIO_MODE_INPUT, pull)

/* ---------------- BK48x9 3-wire bus (bit-banged) ---------------- */
typedef struct {
    gpio_type *cs_port; uint16_t cs;
    gpio_type *ck_port; uint16_t ck;
    gpio_type *da_port; uint16_t da;
} bkbus_t;

static const bkbus_t bk[2] = {
    { GPIOA, GPIO_PINS_8, GPIOF, GPIO_PINS_6, GPIOC, GPIO_PINS_9 },   /* chip 1 */
    { GPIOC, GPIO_PINS_7, GPIOC, GPIO_PINS_8, GPIOC, GPIO_PINS_6 },   /* chip 2 */
};

static void bk_pins(void)
{
    for (int i = 0; i < 2; i++) {
        gpio_bits_set(bk[i].cs_port, bk[i].cs);
        gpio_bits_reset(bk[i].ck_port, bk[i].ck);
        gpio_bits_reset(bk[i].da_port, bk[i].da);
        pin_out(bk[i].cs_port, bk[i].cs);
        pin_out(bk[i].ck_port, bk[i].ck);
        pin_out(bk[i].da_port, bk[i].da);
    }
}

static void bk_byte(const bkbus_t *b, uint8_t v)
{
    for (int i = 0; i < 8; i++) {
        gpio_bits_reset(b->ck_port, b->ck);
        if (v & 0x80) gpio_bits_set(b->da_port, b->da); else gpio_bits_reset(b->da_port, b->da);
        delay_short();
        gpio_bits_set(b->ck_port, b->ck);
        delay_short();
        v <<= 1;
    }
}

static void bk_write(int n, uint8_t reg, uint16_t val)
{
    const bkbus_t *b = &bk[n];
    gpio_bits_reset(b->ck_port, b->ck);
    gpio_bits_reset(b->cs_port, b->cs);
    bk_byte(b, reg & 0x7F);
    bk_byte(b, (uint8_t)(val >> 8));
    bk_byte(b, (uint8_t)val);
    gpio_bits_set(b->cs_port, b->cs);
}
static void bk1_write(uint8_t reg, uint16_t val) { bk_write(0, reg, val); }

static uint16_t bk_read(int n, uint8_t reg)
{
    const bkbus_t *b = &bk[n];
    gpio_bits_reset(b->ck_port, b->ck);
    gpio_bits_reset(b->cs_port, b->cs);
    bk_byte(b, reg | 0x80);
    delay_short();
    pin_in(b->da_port, b->da, GPIO_PULL_NONE);
    gpio_bits_reset(b->ck_port, b->ck);
    uint16_t r = 0;
    for (int i = 0; i < 16; i++) {
        r <<= 1;
        gpio_bits_set(b->ck_port, b->ck);
        if (gpio_input_data_bit_read(b->da_port, b->da) == SET) r |= 1;
        delay_short();
        gpio_bits_reset(b->ck_port, b->ck);
        delay_short();
    }
    gpio_bits_set(b->cs_port, b->cs);
    gpio_bits_reset(b->da_port, b->da);
    pin_out(b->da_port, b->da);
    return r;
}

/* ---------------- transmit path off ---------------- */
/* MCU amplifier pins PC15/PB11 low; BK4819 #1: PA power 0, its GPIO outputs (0x33) 0, RF off,
 * then the stock sleep pair (0x80052B0: 0x30 = 0, 0x37 = 0x1D00). */
static void tx_kill(void)
{
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    bk1_write(0x36, 0x007F);
    bk1_write(0x33, 0x0000);
    bk1_write(0x30, 0x0000);
    bk1_write(0x37, 0x1D00);
}

/* ---------------- supply monitor ---------------- */
/* The power voltage monitor (PWC ctrl: PVMEN bit 4, threshold bits 7:5, 7 = 2.9 V) raises EXINT line 16
 * when VDD falls below 2.9 V, as it does when the power knob is clicked off. The handler switches
 * transmit off while the supply lasts, so the radio chip is not left transmitting (see FINDINGS). */
#define PWC_CTRL    (*(volatile uint32_t *)0x40007000u)
#define EXINT_REG   ((volatile uint32_t *)0x40013C00u)    /* inten, evten, polcfg1, polcfg2, swtrg, intsts */
void PVM_IRQHandler(void)
{
    EXINT_REG[5] = 1u << 16;
    tx_kill();
    gpio_bits_reset(GPIOA, GPIO_PINS_14);
}

static void pvm_init(void)
{
    PWC_CTRL = (PWC_CTRL & ~0xF0u) | (7u << 5) | (1u << 4);
    EXINT_REG[5] = 1u << 16;
    EXINT_REG[2] |= 1u << 16;          /* rising edge of the PVM output = VDD below the threshold */
    EXINT_REG[0] |= 1u << 16;
    NVIC_SetPriority(PVM_IRQn, 0);
    NVIC_EnableIRQ(PVM_IRQn);
}

/* ---------------- system clock ---------------- */
/* As stock (0x8021124, Artery's standard setup): HICK 8 MHz -> PLL x96 /1 /8 = 96 MHz; AHB and APB2
 * 96 MHz, APB1 48 MHz (the display's SPI3 then runs at 24 MHz). Before this the app ran at whatever
 * clock the bootloader left. */
static uint32_t boot_clock;
static void clock_config(void)
{
    boot_clock = system_core_clock;
    crm_reset();
    flash_psr_set(4);                                              /* flash psr = 0x154 */
    crm_periph_clock_enable(CRM_PWC_PERIPH_CLOCK, TRUE);
    volatile uint32_t *ldo = (volatile uint32_t *)(0x40007000u + 0x10u);
    *ldo = (*ldo & ~3u) | 3u;                                      /* LDO 1.3 V, for up to 150 MHz */
    crm_clock_source_enable(CRM_CLOCK_SOURCE_LICK, TRUE);
    while (crm_flag_get(CRM_LICK_STABLE_FLAG) != SET) { }
    crm_clock_source_enable(CRM_CLOCK_SOURCE_HICK, TRUE);
    while (crm_flag_get(CRM_HICK_STABLE_FLAG) != SET) { }
    crm_pll_config(CRM_PLL_SOURCE_HICK, 96, 1, CRM_PLL_FR_8);
    crm_clock_source_enable(CRM_CLOCK_SOURCE_PLL, TRUE);
    while (crm_flag_get(CRM_PLL_STABLE_FLAG) != SET) { }
    crm_ahb_div_set(CRM_AHB_DIV_1);
    crm_apb2_div_set(CRM_APB2_DIV_1);
    crm_apb1_div_set(CRM_APB1_DIV_2);
    crm_auto_step_mode_enable(TRUE);
    crm_sysclk_switch(CRM_SCLK_PLL);
    while (crm_sysclk_switch_status_get() != CRM_SCLK_PLL) { }
    crm_auto_step_mode_enable(FALSE);
    system_core_clock_update();
}

/* ---------------- start-up cleanup and faults ---------------- */
/* The bootloader leaves USART6 and its interrupt enabled when it starts the app. */
static void bootloader_cleanup(void)
{
    __disable_irq();
    SysTick->CTRL = 0;
    for (int i = 0; i < 8; i++) {
        NVIC->ICER[i] = 0xFFFFFFFFu;
        NVIC->ICPR[i] = 0xFFFFFFFFu;
    }
    SCB->VTOR = APP_BASE;
    crm_periph_reset(CRM_USART6_PERIPH_RESET, TRUE);
    crm_periph_reset(CRM_USART6_PERIPH_RESET, FALSE);
    __DSB();
    __ISB();
    __enable_irq();
}

/* Any fault: transmit off, then the red LED (PA14) flashes fast. */
static void fault_blink(void)
{
    __disable_irq();
    tx_kill();
    for (;;) {
        GPIOA->odt ^= GPIO_PINS_14;
        for (volatile uint32_t i = 0; i < 100000; i++) { }
    }
}
void HardFault_Handler(void)  { fault_blink(); }
void MemManage_Handler(void)  { fault_blink(); }
void BusFault_Handler(void)   { fault_blink(); }
void UsageFault_Handler(void) { fault_blink(); }

/* ---------------- serial: USART6, PA11 TX / PA12 RX ---------------- */
static volatile uint8_t rx_buf[128];
static volatile uint8_t rx_head, rx_tail;

static void uart_init(void)
{
    crm_periph_clock_enable(CRM_USART6_PERIPH_CLOCK, TRUE);
    pin_cfg(GPIOA, GPIO_PINS_11, GPIO_MODE_MUX, GPIO_PULL_NONE);
    pin_cfg(GPIOA, GPIO_PINS_12, GPIO_MODE_MUX, GPIO_PULL_UP);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE11, GPIO_MUX_8);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE12, GPIO_MUX_8);
    usart_init(USART6, 115200, USART_DATA_8BITS, USART_STOP_1_BIT);
    usart_transmitter_enable(USART6, TRUE);
    usart_receiver_enable(USART6, TRUE);
    usart_enable(USART6, TRUE);
    usart_interrupt_enable(USART6, USART_RDBF_INT, TRUE);
    NVIC_EnableIRQ(USART6_IRQn);
}

void USART6_IRQHandler(void)
{
    /* Reading the data register clears both the data-ready and the overrun flags. */
    while (usart_flag_get(USART6, USART_RDBF_FLAG) == SET || usart_flag_get(USART6, USART_ROERR_FLAG) == SET) {
        uint8_t b = (uint8_t)usart_data_receive(USART6);
        uint8_t next = (uint8_t)((rx_head + 1) & 127);
        if (next != rx_tail) {
            rx_buf[rx_head] = b;
            rx_head = next;
        }
    }
}

static void uart_putc(char c)
{
    while (usart_flag_get(USART6, USART_TDBE_FLAG) == RESET) { }
    usart_data_transmit(USART6, (uint16_t)c);
}

static void uart_puts(const char *s)
{
    while (*s) uart_putc(*s++);
}

static int uart_getc(void)
{
    if (rx_tail == rx_head) return -1;
    uint8_t b = rx_buf[rx_tail];
    rx_tail = (uint8_t)((rx_tail + 1) & 127);
    return b;
}

static const char HEX[] = "0123456789ABCDEF";

static void put_hex(uint32_t v, int digits)
{
    for (int i = digits - 1; i >= 0; i--) uart_putc(HEX[(v >> (i * 4)) & 0xF]);
}

static void put_dec(int32_t v)
{
    char b[12];
    int n = 0;
    uint32_t u = v < 0 ? (uint32_t)-v : (uint32_t)v;
    do { b[n++] = (char)('0' + u % 10); u /= 10; } while (u);
    if (v < 0) uart_putc('-');
    while (n) uart_putc(b[--n]);
}

/* ---------------- display: ST7789 on SPI3, portrait 240 x 320 ---------------- */
#define LCD_W 240
#define LCD_H 320
#define DC_PIN  GPIO_PINS_13   /* PC13, low = command */
#define RST_PIN GPIO_PINS_14   /* PC14 */

#define RGB(r, g, b) (uint16_t)((((r) & 0xF8) << 8) | (((g) & 0xFC) << 3) | ((b) >> 3))
#define C_BLACK  RGB(0, 0, 0)
#define C_WHITE  RGB(255, 255, 255)
#define C_YELLOW RGB(255, 220, 0)
#define C_BLUE   RGB(0, 40, 140)
#define C_GREEN  RGB(0, 200, 0)
#define C_RED    RGB(230, 0, 0)
#define C_GREY   RGB(130, 130, 130)

static void spi_wait_idle(void)
{
    while (spi_i2s_flag_get(SPI3, SPI_I2S_TDBE_FLAG) == RESET) { }
    while (spi_i2s_flag_get(SPI3, SPI_I2S_BF_FLAG) == SET) { }
}

static void spi_send(uint8_t b)
{
    while (spi_i2s_flag_get(SPI3, SPI_I2S_TDBE_FLAG) == RESET) { }
    spi_i2s_data_transmit(SPI3, b);
}

static void lcd_cmd(uint8_t c)
{
    spi_wait_idle();
    gpio_bits_reset(GPIOC, DC_PIN);
    spi_send(c);
    spi_wait_idle();
    gpio_bits_set(GPIOC, DC_PIN);
}

static void lcd_data(const uint8_t *d, int n)
{
    while (n--) spi_send(*d++);
}

#define LCD_SEQ(cmd, ...) do { static const uint8_t a_[] = { __VA_ARGS__ }; lcd_cmd(cmd); lcd_data(a_, sizeof a_); } while (0)

static void lcd_init(void)
{
    crm_periph_clock_enable(CRM_SPI3_PERIPH_CLOCK, TRUE);
    pin_cfg(GPIOA, GPIO_PINS_15, GPIO_MODE_MUX, GPIO_PULL_NONE);
    gpio_pin_mux_config(GPIOA, GPIO_PINS_SOURCE15, GPIO_MUX_6);
    pin_cfg(GPIOB, GPIO_PINS_3 | GPIO_PINS_5, GPIO_MODE_MUX, GPIO_PULL_NONE);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE3, GPIO_MUX_6);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE5, GPIO_MUX_6);
    gpio_bits_reset(GPIOC, DC_PIN | RST_PIN);
    pin_out(GPIOC, DC_PIN | RST_PIN);

    spi_init_type s;
    spi_default_para_init(&s);
    s.transmission_mode = SPI_TRANSMIT_HALF_DUPLEX_TX;
    s.master_slave_mode = SPI_MODE_MASTER;
    s.mclk_freq_division = SPI_MCLK_DIV_2;
    s.first_bit_transmission = SPI_FIRST_BIT_MSB;
    s.frame_bit_num = SPI_FRAME_8BIT;
    s.clock_polarity = SPI_CLOCK_POLARITY_HIGH;
    s.clock_phase = SPI_CLOCK_PHASE_1EDGE;
    s.cs_mode_selection = SPI_CS_HARDWARE_MODE;
    spi_init(SPI3, &s);
    spi_hardware_cs_output_enable(SPI3, TRUE);
    spi_enable(SPI3, TRUE);

    /* Same sequence as the stock firmware (0x8017E48). */
    gpio_bits_set(GPIOC, RST_PIN);   delay_ms(1);
    gpio_bits_reset(GPIOC, RST_PIN); delay_ms(1);
    gpio_bits_set(GPIOC, RST_PIN);   delay_ms(120);
    lcd_cmd(0x11);                   delay_ms(120);
    LCD_SEQ(0x36, 0xA8);
    LCD_SEQ(0x3A, 0x05);
    LCD_SEQ(0xB2, 0x0C, 0x0C, 0x00, 0x33, 0x33);
    LCD_SEQ(0xB7, 0x35);
    LCD_SEQ(0xBB, 0x2B);
    LCD_SEQ(0xC0, 0x2C);
    LCD_SEQ(0xC2, 0x01);
    LCD_SEQ(0xC3, 0x23);
    LCD_SEQ(0xC4, 0x20);
    LCD_SEQ(0xC6, 0x0F);
    LCD_SEQ(0xD0, 0xA4, 0xA1);
    LCD_SEQ(0xE0, 0xD0, 0x00, 0x05, 0x0E, 0x15, 0x0D, 0x37, 0x43, 0x47, 0x09, 0x15, 0x12, 0x16, 0x19);
    LCD_SEQ(0xE1, 0xD0, 0x00, 0x05, 0x0D, 0x0C, 0x06, 0x2D, 0x44, 0x40, 0x0E, 0x1C, 0x18, 0x16, 0x19);
    LCD_SEQ(0xE7, 0x10);
    lcd_cmd(0x29);
}

/* Controller column = 319 - y (the stock text routine moves down a line by subtracting
 * from y). Pixels stream one x column at a time, each column from bottom to top. */
static void lcd_window(int x0, int y0, int x1, int y1)
{
    int c0 = LCD_H - 1 - y1, c1 = LCD_H - 1 - y0;
    uint8_t c[4] = { (uint8_t)(c0 >> 8), (uint8_t)c0, (uint8_t)(c1 >> 8), (uint8_t)c1 };
    uint8_t r[4] = { (uint8_t)(x0 >> 8), (uint8_t)x0, (uint8_t)(x1 >> 8), (uint8_t)x1 };
    lcd_cmd(0x2A); lcd_data(c, 4);
    lcd_cmd(0x2B); lcd_data(r, 4);
    lcd_cmd(0x2C);
}

static void lcd_fill(int x, int y, int w, int h, uint16_t col)
{
    lcd_window(x, y, x + w - 1, y + h - 1);
    for (int32_t n = (int32_t)w * h; n > 0; n--) {
        spi_send((uint8_t)(col >> 8));
        spi_send((uint8_t)col);
    }
}

/* ---------------- anti-aliased text (fonts_aa.h, 4-bit alpha) ---------------- */
#define C_BAR      RGB(16, 22, 38)
#define C_DIM      RGB(120, 130, 150)
#define C_LIGHT    RGB(205, 210, 220)
#define C_SEGOFF   RGB(38, 42, 54)
#define C_CHIP     RGB(30, 90, 200)
#define C_GREEN_D  RGB(0, 140, 60)
#define C_ORANGE   RGB(240, 130, 0)
#define C_PURPLE   RGB(120, 60, 200)
#define C_TEAL     RGB(0, 125, 135)

static const uint8_t edge_map[4][16] = {
    { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15 },
    { 0, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 13, 14, 15 },
    { 0, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 12, 13, 14, 14, 15 },
    { 0, 4, 5, 7, 8, 9, 9, 10, 11, 12, 12, 13, 13, 14, 14, 15 }
};
static int edge = 2;
/* Screen style, switchable over serial (FONT, EDGE, PAL); defaults chosen on the radio 2026-11-14. */
typedef struct {
    uint16_t bar, dim, light, sep, chip_text, mode, pw[4], mic, txt, rxt, b_rx, b_warn, b_tx, b_entry;
} palette_t;
static const palette_t palettes[2] = {
    { RGB(16, 22, 38), RGB(120, 130, 150), RGB(205, 210, 220), RGB(48, 58, 80), RGB(255, 255, 255),
      RGB(30, 90, 200), { RGB(130, 130, 130), RGB(0, 140, 60), RGB(240, 130, 0), RGB(230, 0, 0) },
      RGB(130, 130, 130), RGB(120, 60, 200), RGB(0, 125, 135),
      RGB(0, 95, 50), RGB(150, 100, 0), RGB(200, 0, 0), RGB(30, 70, 160) },
    { RGB(0, 0, 0), RGB(255, 255, 255), RGB(255, 255, 255), RGB(110, 120, 150), RGB(0, 0, 0),
      RGB(0, 200, 255), { RGB(180, 180, 180), RGB(0, 230, 0), RGB(255, 170, 0), RGB(255, 70, 70) },
      RGB(210, 210, 210), RGB(220, 130, 255), RGB(0, 230, 210),
      RGB(0, 120, 0), RGB(170, 90, 0), RGB(220, 0, 0), RGB(0, 70, 200) },
};
static const palette_t *P = &palettes[1];
static const aafont_t *F_MED = &font_med2, *F_SMALL = &font_small2;

static uint16_t mix565(uint16_t bg, uint16_t fg, int a)
{
    int br = bg >> 11, bgr = (bg >> 5) & 63, bb = bg & 31;
    int fr = fg >> 11, fgr = (fg >> 5) & 63, fb = fg & 31;
    int r = br + (fr - br) * a / 15, g = bgr + (fgr - bgr) * a / 15, b = bb + (fb - bb) * a / 15;
    return (uint16_t)(r << 11 | g << 5 | b);
}

/* One glyph with its top-left corner at (x, y); returns its advance width. */
static int aa_char(int x, int y, const aafont_t *f, char ch, uint16_t fg, uint16_t bg)
{
    uint8_t c = (uint8_t)ch;
    if (c < f->first || c > f->last) return 0;
    int w = f->width[c - f->first], h = f->height;
    if (!w || x < 0 || y < 0 || x + w > LCD_W || y + h > LCD_H) return w;
    const uint8_t *d = f->data + f->offset[c - f->first];
    uint16_t ramp[16];
    for (int a = 0; a < 16; a++) ramp[a] = mix565(bg, fg, edge_map[edge][a]);
    lcd_window(x, y, x + w - 1, y + h - 1);
    for (int col = 0; col < w; col++) {
        for (int row = h - 1; row >= 0; row--) {
            int k = row * w + col;
            int a = (k & 1) ? (d[k >> 1] & 0x0F) : (d[k >> 1] >> 4);
            spi_send((uint8_t)(ramp[a] >> 8));
            spi_send((uint8_t)ramp[a]);
        }
    }
    return w;
}

static int aa_width(const aafont_t *f, const char *s)
{
    int w = 0;
    for (; *s; s++) {
        uint8_t c = (uint8_t)*s;
        if (c >= f->first && c <= f->last) w += f->width[c - f->first];
    }
    return w;
}

static int aa_text(int x, int y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    for (; *s; s++) x += aa_char(x, y, f, *s, fg, bg);
    return x;
}

static void aa_text_c(int y, const aafont_t *f, const char *s, uint16_t fg, uint16_t bg)
{
    aa_text((LCD_W - aa_width(f, s)) / 2, y, f, s, fg, bg);
}

/* Filled rectangle with rounded corners (radius 4); the corners keep what was there. */
static void fill_rrect(int x, int y, int w, int h, uint16_t col)
{
    static const uint8_t inset[4] = { 3, 2, 1, 1 };
    for (int k = 0; k < 4; k++) {
        lcd_fill(x + inset[k], y + k, w - 2 * inset[k], 1, col);
        lcd_fill(x + inset[k], y + h - 1 - k, w - 2 * inset[k], 1, col);
    }
    lcd_fill(x, y + 4, w, h - 8, col);
}

/* Boot splash: the callsign inverted (black on white) with two lines under it, held 4 s. Always this callsign. */
#define SPLASH_CALL "M0FCC"
static void splash(void)
{
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
    lcd_fill(0, 26, LCD_W, 3, C_YELLOW);
    lcd_fill(0, 291, LCD_W, 3, C_YELLOW);
    const int bx = 8, by = 64, bw = LCD_W - 16, bh = font_call.height + 8;
    fill_rrect(bx, by, bw, bh, C_WHITE);
    aa_text_c(by + 4, &font_call, SPLASH_CALL, C_BLACK, C_WHITE);
    aa_text_c(by + bh + 28, &font_med, "iRadio UV98+", C_YELLOW, C_BLACK);
    aa_text_c(by + bh + 64, &font_med, "Open Firmware v1.0", C_WHITE, C_BLACK);
    delay_ms(4000);
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
}

/* ---------------- backlight: DAC1 on PA4 ---------------- */
static void backlight(uint8_t level)
{
    crm_periph_clock_enable(CRM_DAC_PERIPH_CLOCK, TRUE);
    pin_cfg(GPIOA, GPIO_PINS_4, GPIO_MODE_ANALOG, GPIO_PULL_NONE);
    dac_output_buffer_enable(DAC1_SELECT, TRUE);
    dac_enable(DAC1_SELECT, TRUE);
    dac_1_data_set(DAC1_8BIT_RIGHT, level);
}

/* ---------------- radio: BK4819 #1 ---------------- */
/* Frequencies are in 10 Hz units throughout, as in the stock firmware. */
typedef struct {
    uint32_t lo, hi;      /* [lo, hi) */
    uint16_t cal;         /* frequency correction word from this radio's calibration */
    uint8_t  rx_var;      /* receive filter selector (stock 0x20000839) */
    uint8_t  tx_var;      /* transmit path selector (stock 0x20000838) */
} band_t;

/* Band edges and selectors from stock 0x8012A30; correction words read from
 * backups/uv98plus_spiflash_4MB_2026-11-13_evening.bin (offset 0x20 + band * 0x70). */
static const band_t bands[] = {
    {   1800000,   3200000, 0x8000, 1, 1 },
    {   3200000,   4500000, 0x8000, 1, 3 },
    {   4500000,   6400000, 0x8000, 2, 3 },
    {   6400000,  13600000, 0x8000, 3, 3 },
    {  13600000,  17400000, 0x801A, 3, 3 },
    {  17400000,  24000000, 0x8037, 3, 3 },
    {  24000000,  26000000, 0x8000, 3, 3 },
    {  26000000,  32000000, 0x8000, 4, 4 },
    {  32000000,  40000000, 0x801E, 4, 4 },
    {  40000000,  48000000, 0x8040, 4, 4 },
    {  48000000,  56000000, 0x8000, 4, 4 },
    {  56000000,  66000000, 0x8000, 4, 4 },
    {  84000000, 130000000, 0x8000, 5, 5 },
};
#define NBANDS (sizeof bands / sizeof bands[0])

/* ---------------- SPI flash and calibration ---------------- */
/* SPI2: PB13 SCK, PB14 MISO, PB15 MOSI (mux 5), PB12 chip select (driven high by stock 0x8010C74).
 * Read only here: 0x9F (JEDEC ID) and 0x03 (read data). */
static void flash_init(void)
{
    crm_periph_clock_enable(CRM_SPI2_PERIPH_CLOCK, TRUE);
    gpio_bits_set(GPIOB, GPIO_PINS_12);
    pin_out(GPIOB, GPIO_PINS_12);
    pin_cfg(GPIOB, GPIO_PINS_13 | GPIO_PINS_14 | GPIO_PINS_15, GPIO_MODE_MUX, GPIO_PULL_NONE);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE13, GPIO_MUX_5);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE14, GPIO_MUX_5);
    gpio_pin_mux_config(GPIOB, GPIO_PINS_SOURCE15, GPIO_MUX_5);
    spi_init_type s;
    spi_default_para_init(&s);
    s.transmission_mode = SPI_TRANSMIT_FULL_DUPLEX;
    s.master_slave_mode = SPI_MODE_MASTER;
    s.mclk_freq_division = SPI_MCLK_DIV_8;
    s.first_bit_transmission = SPI_FIRST_BIT_MSB;
    s.frame_bit_num = SPI_FRAME_8BIT;
    s.clock_polarity = SPI_CLOCK_POLARITY_LOW;
    s.clock_phase = SPI_CLOCK_PHASE_1EDGE;
    s.cs_mode_selection = SPI_CS_SOFTWARE_MODE;
    spi_init(SPI2, &s);
    spi_software_cs_internal_level_set(SPI2, SPI_SWCS_INTERNAL_LEVEL_HIGHT);
    spi_enable(SPI2, TRUE);
}

static uint8_t flash_xfer(uint8_t b)
{
    while (spi_i2s_flag_get(SPI2, SPI_I2S_TDBE_FLAG) == RESET) { }
    spi_i2s_data_transmit(SPI2, b);
    while (spi_i2s_flag_get(SPI2, SPI_I2S_RDBF_FLAG) == RESET) { }
    return (uint8_t)spi_i2s_data_receive(SPI2);
}

static void flash_read(uint32_t addr, uint8_t *buf, int n)
{
    gpio_bits_reset(GPIOB, GPIO_PINS_12);
    flash_xfer(0x03);
    flash_xfer((uint8_t)(addr >> 16));
    flash_xfer((uint8_t)(addr >> 8));
    flash_xfer((uint8_t)addr);
    for (int i = 0; i < n; i++) buf[i] = flash_xfer(0xFF);
    gpio_bits_set(GPIOB, GPIO_PINS_12);
}

static uint32_t flash_id(void)
{
    gpio_bits_reset(GPIOB, GPIO_PINS_12);
    flash_xfer(0x9F);
    uint32_t id = (uint32_t)flash_xfer(0xFF) << 16;
    id |= (uint32_t)flash_xfer(0xFF) << 8;
    id |= flash_xfer(0xFF);
    gpio_bits_set(GPIOB, GPIO_PINS_12);
    return id;
}

/* The radio's own calibration (stock 0x8013E3C / 0x8012A30): a 0x20-byte header at 0x0000 and sixteen
 * 0x70-byte blocks from 0x0020. Per block: +0 frequency correction word, +3 deviation (wide), +10
 * deviation (narrow), +0x10 high power and +0x20 low power entries (16 each, one per 5 MHz from the
 * block's base). Header bytes 8-11: audio response indexes (TX 300 Hz, TX 3 kHz, RX 300 Hz, RX 3 kHz).
 * It is used only if every block in use passes the checks in cal_load(); otherwise the values built
 * in from our radio's backup are used. */
static uint8_t cal_hdr[0x20], cal_blk[16][0x70];
static int cal_ok;
static uint32_t flash_jedec;
static const struct { uint32_t lo, hi, base; uint8_t blk; } cal_map[] = {   /* inclusive, in stock's order */
    {  13600000,  17400000,  13500000,  0 }, {  40000000,  48000000,  40000000,  1 },
    {   1800000,   6400000,   1500000,  2 }, {   6400000,  13600000,   6000000,  3 },
    {  17400000,  24000000,  17000000,  4 }, {  24000000,  32000000,  24000000,  5 },
    {  32000000,  40000000,  32000000,  6 }, {  48000000,  56000000,  48000000,  7 },
    {  56000000,  66000000,  56000000,  8 }, {  84000000,  92000000,  84000000,  9 },
    {  92000000, 100000000,  92000000, 10 }, { 100000000, 108000000, 100000000, 11 },
    { 108000000, 116000000, 108000000, 12 }, { 116000000, 124000000, 116000000, 13 },
    { 124000000, 130000000, 124000000, 13 },
};
#define NCALMAP ((int)(sizeof cal_map / sizeof cal_map[0]))
/* Audio response coefficients by index (stock 0x80038D0). */
static const uint16_t af300[7][2] = { { 0x8BD7, 0x3507 }, { 0x8AED, 0x35E1 }, { 0x8A96, 0x3606 }, { 0x8908, 0x3779 },
                                      { 0x8DDE, 0x335A }, { 0x8EB3, 0x32BA }, { 0x8F88, 0x3201 } };
static const uint16_t af3k[5] = { 0xE61C, 0xDF22, 0xD42D, 0xCC35, 0xFA02 };

static uint16_t le16(const uint8_t *b) { return (uint16_t)(b[0] | b[1] << 8); }

static int cal_index(uint32_t f)
{
    if (!cal_ok) return -1;
    for (int i = 0; i < NCALMAP; i++) if (f >= cal_map[i].lo && f <= cal_map[i].hi) return i;
    return -1;
}

static void cal_load(void)
{
    cal_ok = 0;
    flash_jedec = flash_id();
    if ((flash_jedec >> 16) == 0x00 || (flash_jedec >> 16) == 0xFF) return;
    flash_read(0, cal_hdr, sizeof cal_hdr);
    flash_read(0x20, &cal_blk[0][0], sizeof cal_blk);
    if (cal_hdr[8] > 6 || cal_hdr[9] > 4 || cal_hdr[10] > 6 || cal_hdr[11] > 4) return;
    for (int i = 0; i < NCALMAP; i++) {
        const uint8_t *b = cal_blk[cal_map[i].blk];
        uint16_t w = le16(b), d3 = le16(b + 3), d10 = le16(b + 10);
        if (w < 0x7E00 || w > 0x81FF || (d3 & 0xF000) != 0x3000 || (d10 & 0xF000) != 0x3000) return;
        for (int k = 0; k < 32; k++) if (b[0x10 + k] > 0x3F) return;
    }
    cal_ok = 1;
}

/* Frequency correction word at f: from the radio's calibration, or the built-in band value. */
static uint16_t cal_word(uint32_t f, uint16_t builtin)
{
    int i = cal_index(f);
    return i >= 0 ? le16(cal_blk[cal_map[i].blk]) : builtin;
}

/* Transmit deviation at f (stock 0x8004996: +3 wide, +10 narrow); 0x34E2 built in. */
static uint16_t cal_dev(uint32_t f, int narrow)
{
    int i = cal_index(f);
    return i >= 0 ? le16(cal_blk[cal_map[i].blk] + (narrow ? 10 : 3)) : 0x34E2;
}

/* Transmit is allowed only inside these ranges (10 Hz units, [lo, hi)): the amateur allocations of
 * all three ITU regions that this chip can tune (all power levels), and marine VHF (capped at MID,
 * meant as about 5 W; not measured). Air band and everything else: no transmit. */
static const struct { uint32_t lo, hi; uint8_t maxpow; } tx_ranges[] = {
    { 1806800, 1816800, 3 },   /* 17 m   18.068-18.168 */
    { 2100000, 2145000, 3 },   /* 15 m   21.000-21.450 */
    { 2489000, 2499000, 3 },   /* 12 m   24.890-24.990 */
    { 2800000, 2970000, 3 },   /* 10 m   28.000-29.700 */
    { 4066000, 4070000, 3 },   /* 8 m    40.660-40.700 (some Region 1 countries) */
    { 5000000, 5400000, 3 },   /* 6 m    50-54 */
    { 6990000, 7050000, 3 },   /* 4 m    69.9-70.5 (some Region 1 countries) */
    { 14400000, 14800000, 3 },   /* 2 m    144-148 */
    { 21900000, 22500000, 3 },   /* 1.25 m 219-225 (Region 2) */
    { 42000000, 45000000, 3 },   /* 70 cm  420-450 (includes PMR446) */
    { 90200000, 92800000, 3 },   /* 33 cm  902-928 (Region 2) */
    { 124000000, 130000000, 3 },   /* 23 cm  1240-1300 */
    {  15600000,  16202500, 2 },   /* marine VHF 156.000-162.025, MID at most */
};
/* Transmit power. MIN (the default): BK4819 PA level 8 with no external amplifier. LOW, MID, HIGH:
 * stock levels (0x80053F0 / 0x8004F0C) from this radio's calibration, with the band's amplifier on. */
enum { PWR_MIN, PWR_LOW, PWR_MID, PWR_HIGH };
static const char *const power_name[] = { "MIN", "LOW", "MID", "HIGH" };
static int power = PWR_MIN;
#define TX_POWER_MIN ((8u << 10) | 0xFF)

/* This radio's power calibration (SPI 0x20 + block * 0x70: +0x10 high, +0x20 low), one entry per
 * 5 MHz from the base, capped at entry 15 (stock 0x8012A30). Ranges inclusive, checked in order. */
typedef struct { uint32_t lo, hi, base; uint8_t high[16], low[16]; } pwr_cal_t;
static const pwr_cal_t pwr_cal[] = {
    {  13600000,  17400000,  13500000,   /* block 0 */
       { 0x28,0x28,0x27,0x28,0x29,0x29,0x29,0x2B,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37 },
       { 0x14,0x14,0x14,0x14,0x14,0x15,0x16,0x17,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
    {  40000000,  48000000,  40000000,   /* block 1 */
       { 0x2C,0x29,0x29,0x29,0x2A,0x2B,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C,0x2C },
       { 0x1C,0x1B,0x1A,0x19,0x18,0x17,0x17,0x17,0x17,0x16,0x16,0x16,0x16,0x16,0x16,0x16 } },
    {   1800000,   6400000,   1500000,   /* block 2 */
       { 0x37,0x37,0x23,0x26,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
    {   6400000,  13600000,   6000000,   /* block 3 */
       { 0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
    {  17400000,  24000000,  17000000,   /* block 4 */
       { 0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x2D,0x0F,0x37 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x0F,0x0F,0x19 } },
    {  84000000,  92000000,  84000000,   /* block 9 */
       { 0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32,0x32 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
    {  92000000, 100000000,  92000000,   /* block 10 */
       { 0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
    { 124000000, 130000000, 124000000,   /* block 13 from 1240 MHz */
       { 0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37,0x37 },
       { 0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19,0x19 } },
};

/* 0x36 value for the current power level at f; 0 means use TX_POWER_MIN without the amplifier. */
static uint16_t power_reg(uint32_t f, int power)
{
    if (power == PWR_MIN) return 0;
    int ci = cal_index(f);
    if (ci >= 0) {
        const uint8_t *b = cal_blk[cal_map[ci].blk];
        uint32_t idx = (f - cal_map[ci].base) / 500000u;
        if (idx > 15) idx = 15;
        uint8_t hi = b[0x10 + idx], lo = b[0x20 + idx];
        uint8_t v = power == PWR_HIGH ? hi : power == PWR_LOW ? lo : (uint8_t)((hi + lo) >> 1);
        return (uint16_t)(((v & 0x3Fu) << 10) | 0x7Fu | (v ? 0x80u : 0));
    }
    for (unsigned i = 0; i < sizeof pwr_cal / sizeof pwr_cal[0]; i++) {
        const pwr_cal_t *c = &pwr_cal[i];
        if (f < c->lo || f > c->hi) continue;
        uint32_t idx = (f - c->base) / 500000u;
        if (idx > 15) idx = 15;
        uint8_t v = power == PWR_HIGH ? c->high[idx]
                  : power == PWR_LOW  ? c->low[idx]
                  : (uint8_t)((c->high[idx] + c->low[idx]) >> 1);
        return (uint16_t)(((v & 0x3Fu) << 10) | 0x7Fu | (v ? 0x80u : 0));
    }
    return 0;
}
/* Mic gain 0-31 (stock menu "MIC Gain", settings 0xAD; this radio 0x12, too quiet in test 9);
 * 0x7D = 0xE940 + gain. */
static int mic_gain = 25;
#define TX_DEVIATION 0x34E2   /* 400-480 MHz calibration block, offset 3 */
/* Time-out timer, seconds (menu). */
static const uint16_t tot_list[] = { 15, 30, 60, 90, 120, 180, 300, 600 };
static int tot_idx = 2;                /* 60 s */
/* Tuning step, 10 Hz units (menu). */
static const uint16_t step_list[] = { 10, 100, 250, 500, 625, 1000, 1250, 2500, 5000, 10000 };
static int step_idx = 6;               /* 12.5 kHz */
/* Backlight levels (stock 0x801FC44: 60, 65, 70, 76, 255). */
static const uint8_t bl_list[] = { 60, 65, 70, 76, 255 };
static int bl_idx = 4;
/* Backlight timeout (menu "Light off"); the keypad light (PA9) follows the backlight. */
static const uint8_t light_list[] = { 0, 10, 20, 30, 60 };          /* seconds, 0 = always on */
static const char *const light_name[] = { "ALWAYS", "10 s", "20 s", "30 s", "60 s" };
static int light_idx = 0, lit = 1, locked, scanning, spectrum_on, fdet_on;
static uint32_t fdet_next, fdet_until;
static uint32_t last_activity, scan_next, scan_hold_until;

/* 1750 Hz tone burst: tone 1 frequency word, Hz * 10.3 (the stock tone formula, Hz / 10 * 0x67). */
#define TONE_1750    (175u * 0x67u)

/* Transmit tone: CTCSS (value in 0.1 Hz) or DCS normal / inverted (value = the octal code). */
enum { TONE_OFF, TONE_CTCSS, TONE_DCS_N, TONE_DCS_I };
static int tone_type;
static uint16_t tone_val;
static int rx_tone_type;          /* tone squelch on receive, same types */
static uint16_t rx_tone_val;
static void rx_tone(void);

enum { MODE_FM, MODE_NFM, MODE_AM, MODE_USB, NMODES };
static const char *const mode_name[] = { "FM", "NFM", "AM", "USB" };

static uint32_t freq10 = 14550000;   /* start on 145.500 MHz (VFO A) */
static int mode = MODE_NFM;
static int tx_on;

static const band_t *band_of(uint32_t f)
{
    for (unsigned i = 0; i < NBANDS; i++)
        if (f >= bands[i].lo && f < bands[i].hi) return &bands[i];
    return 0;
}

/* Filter bandwidth (narrow for NFM and USB); 0x73 bit 4 set for USB as stock does for SSB
 * (0x8004BF0). Audio stays muted until the squelch opens it. */
static void radio_mode(void)
{
    bk1_write(0x43, mode == MODE_NFM || mode == MODE_USB ? 0x4048 : 0x3028);
    uint16_t r73 = bk_read(0, 0x73);
    bk1_write(0x73, mode == MODE_USB ? (r73 | 0x0010) : (r73 & ~0x0010u));
    bk1_write(0x47, 0x6040);
}

static void radio_rx_on(void)
{
    bk1_write(0x37, 0x1F0F);
    delay_ms(10);
    bk1_write(0x30, 0x0200);
    bk1_write(0x30, 0xBFF1);
}

/* Receive filter bits in 0x33 (stock 0x8004FF0); bits 2 and 3 (TX amplifiers) forced to 0. */
static void radio_rx_path(const band_t *b)
{
    uint16_t v = bk_read(0, 0x33) & 0xFFC0;
    switch (b->rx_var) {
    case 1: case 2: v |= 0x01; break;
    case 3: v |= 0x10; break;
    case 4: v |= 0x20; break;
    case 5: v |= 0x02; break;
    }
    bk1_write(0x33, v);
    if (b->rx_var == 3 || b->rx_var == 4) gpio_bits_reset(GPIOF, GPIO_PINS_0);
    else gpio_bits_set(GPIOF, GPIO_PINS_0);
}

static int radio_tune(uint32_t f)
{
    const band_t *b = band_of(f);
    if (!b) return 0;
    freq10 = f;
    radio_rx_path(b);
    uint32_t v = f + cal_word(f, b->cal) - 0x8000u;
    bk1_write(0x38, (uint16_t)(v & 0xFFFF));
    bk1_write(0x39, (uint16_t)(v >> 16));
    radio_rx_on();
    rx_tone();
    return 1;
}

/* Stock 0x8004DC0, without its call to 0x8005698 (which writes the TX value 0xC1FE). */
static void radio_init(void)
{
    bk1_write(0x00, 0x8000);
    delay_ms(10);
    bk1_write(0x00, 0x0000);
    bk1_write(0x37, 0x1D0F);
    bk1_write(0x13, 0x03BE);
    bk1_write(0x12, 0x037B);
    bk1_write(0x11, 0x027B);
    bk1_write(0x10, 0x007A);
    bk1_write(0x14, 0x0019);
    bk1_write(0x49, 0x2A38);
    bk1_write(0x7B, 0x8420);
    bk1_write(0x33, 0x0000);
    bk1_write(0x34, 0x0000);
    bk1_write(0x35, 0x0000);
    bk1_write(0x1E, 0x4C58);
    bk1_write(0x1F, 0xA656);
    bk1_write(0x3E, 0xA037);
    bk1_write(0x7D, 0xE950);
    bk1_write(0x26, 0x13A0);
    /* Audio response coefficients (stock 0x80038D0), indexes from this radio's calibration
     * header SPI 0x0008-0x000B = 06 00 06 04: RX 300 Hz, RX 3 kHz, TX 3 kHz, TX 300 Hz. */
    if (cal_ok) {   /* the radio's own indexes */
        bk1_write(0x54, af300[cal_hdr[10]][0]);
        bk1_write(0x55, af300[cal_hdr[10]][1]);
        bk1_write(0x75, af3k[cal_hdr[11]]);
        bk1_write(0x74, af3k[cal_hdr[9]]);
        bk1_write(0x44, af300[cal_hdr[8]][0]);
        bk1_write(0x45, af300[cal_hdr[8]][1]);
    } else {
        bk1_write(0x54, 0x8F88);
        bk1_write(0x55, 0x3201);
        bk1_write(0x75, 0xFA02);
        bk1_write(0x74, 0xE61C);
        bk1_write(0x44, 0x8F88);
        bk1_write(0x45, 0x3201);
    }
    bk1_write(0x71, 0x68DC);                                     /* scrambler off (0x8005218) */
    bk1_write(0x31, bk_read(0, 0x31) & ~0x000Au);                 /* scrambler and compander off */
    bk1_write(0x51, 0x0000);                                     /* no CTCSS/DCS */
    radio_mode();
    radio_tune(freq10);

    /* Chip 2 is not used yet: stock reset (0x8003D34), then the stock sleep pair (0x8003EC0). */
    bk_write(1, 0x00, 0x8000);
    delay_ms(10);
    bk_write(1, 0x00, 0x0000);
    bk_write(1, 0x30, 0x0000);
    bk_write(1, 0x37, 0x1D00);
}

static int rssi_dbm(void)
{
    return (int)(bk_read(0, 0x67) & 0x1FF) / 2 - 160;
}

/* ---------------- receive audio (stock 0x8005098 / 0x800AFCC / 0x8004BF0) ---------------- */
/* Speaker amplifier PF8 (high = on), green LED PA13. Gain 0x48 from this radio's settings
 * 0xAE/0xAF (FM) and 0xB0/0xB1 (AM): 0xB32F / 0xB32C; bits 9-4 are the volume. */
static int volume = 0x32;
static int audio_on;
/* Squelch level 0-9 (0 = open). FM/NFM: BK4819 noise (0x65) opens below 70 - 5 * level and
 * closes 10 above (measured here: empty channel 78-83, carrier 13-15). AM: RSSI above the
 * empty-channel floor (about -80 on this radio) by 2 dB per level. */
static int sq_level = 3;

static uint16_t rx_gain(void)
{
    return (uint16_t)(0xB000 | ((volume & 0x3F) << 4) | (mode == MODE_AM || mode == MODE_USB ? 0xC : 0xF));
}

/* AF mode (stock 0x8005098): 1 FM, 7 AM, 5 SSB. */
static void audio_open(void)
{
    bk1_write(0x48, rx_gain());
    bk1_write(0x47, (uint16_t)(0x6040 | ((mode == MODE_AM ? 7 : mode == MODE_USB ? 5 : 1) << 8)));
    delay_ms(10);
    gpio_bits_set(GPIOF, GPIO_PINS_8);
    gpio_bits_set(GPIOA, GPIO_PINS_13);
    audio_on = 1;
}

static void audio_close(void)
{
    gpio_bits_reset(GPIOF, GPIO_PINS_8);
    bk1_write(0x47, 0x6040);
    gpio_bits_reset(GPIOA, GPIO_PINS_13);
    audio_on = 0;
}

/* ---------------- ADC: PC2 side-key ladder (channel 12), PA7 (channel 7) ---------------- */
static void adc_setup(void)
{
    crm_periph_clock_enable(CRM_ADC1_PERIPH_CLOCK, TRUE);
    pin_cfg(GPIOA, GPIO_PINS_7, GPIO_MODE_ANALOG, GPIO_PULL_NONE);
    pin_cfg(GPIOC, GPIO_PINS_2, GPIO_MODE_ANALOG, GPIO_PULL_NONE);
    adc_common_config_type c;
    adc_common_default_para_init(&c);
    c.div = ADC_HCLK_DIV_4;
    adc_common_config(&c);
    adc_base_config_type b;
    adc_base_default_para_init(&b);
    b.sequence_mode = FALSE;
    b.repeat_mode = FALSE;
    b.data_align = ADC_RIGHT_ALIGNMENT;
    b.ordinary_channel_length = 1;
    adc_base_config(ADC1, &b);
    adc_resolution_set(ADC1, ADC_RESOLUTION_12B);
    adc_ordinary_conversion_trigger_set(ADC1, ADC_ORDINARY_TRIG_SOFTWARE, ADC_ORDINARY_TRIG_EDGE_NONE);
    adc_enable(ADC1, TRUE);
    uint32_t t = ticks;
    while (adc_flag_get(ADC1, ADC_RDY_FLAG) == RESET && ticks - t < 10) { }
    adc_calibration_init(ADC1);
    while (adc_calibration_init_status_get(ADC1) == SET && ticks - t < 20) { }
    adc_calibration_start(ADC1);
    while (adc_calibration_status_get(ADC1) == SET && ticks - t < 30) { }
}

static uint16_t adc_read(adc_channel_select_type ch)
{
    adc_ordinary_channel_set(ADC1, ch, 1, ADC_SAMPLETIME_47_5);
    adc_ordinary_software_trigger_enable(ADC1, TRUE);
    uint32_t t = ticks;
    while (adc_flag_get(ADC1, ADC_OCCE_FLAG) == RESET) if (ticks - t > 2) return 0xFFFF;
    return adc_ordinary_conversion_data_get(ADC1);
}

/* Stock ladder decode 0x8013D68: <1885 side key 2, <2717 side key 1, <3542 alarm, else none. */
enum { SIDE_NONE, SIDE_KEY1, SIDE_KEY2, SIDE_ALARM };
static const char *const side_name[] = { "NONE", "SIDE1", "SIDE2", "ALARM" };

static int side_key(void)
{
    uint16_t v = adc_read(ADC_CHANNEL_12);
    if (v < 1885) return SIDE_KEY2;
    if (v < 2717) return SIDE_KEY1;
    if (v < 3542) return SIDE_ALARM;
    return SIDE_NONE;
}

/* Repeater: shift 0 off, +1 or -1; the offset comes from this list (10 Hz units, menu). */
static const uint32_t ofs_list[] = { 10000, 50000, 60000, 100000, 160000, 500000, 600000, 760000, 1200000 };
static const char *const ofs_name[] = { "100k", "500k", "600k", "1M", "1.6M", "5M", "6M", "7.6M", "12M" };
#define NOFS ((int)(sizeof ofs_list / sizeof ofs_list[0]))
static int shift = 0;
static int ofs_idx = 2;                /* 600 kHz */
static uint32_t tx_f;                  /* frequency of the current transmission */
/* Serial PTT: PTT ON must be repeated within 2 s or transmit drops (a crashed program cannot
 * leave the radio on air). Band limits, power and the time-out timer apply as for PTT1. */
#define SERIAL_PTT_MS 2000u
static int serial_ptt;
static uint32_t serial_ptt_until;

static uint32_t tx_freq(void)
{
    return shift > 0 ? freq10 + ofs_list[ofs_idx] : shift < 0 ? freq10 - ofs_list[ofs_idx] : freq10;
}

/* Highest power level allowed at f plus one, or 0 if transmit is not allowed there. */
static int tx_allowed(uint32_t f)
{
    for (unsigned i = 0; i < sizeof tx_ranges / sizeof tx_ranges[0]; i++)
        if (f >= tx_ranges[i].lo && f < tx_ranges[i].hi) return tx_ranges[i].maxpow + 1;
    return 0;
}
static int tx_power;                   /* level actually used for the current transmission */

/* Voice TX (stock 0x8022270 PTT -> 0x801F828 / 0x8004518, then 0x800B22C -> 0x80053F0 /
 * 0x80052D4 with 0x30 = 0xC1FE), amateur bands only, minimum power, no external amplifier.
 * Red LED (PA14) on, green (PA13) off. */
static int tx_burst;

/* DCS codeword: the 12-bit word (code + 0x800) bit-reversed, Golay (23,12) parity with 0xAE3,
 * then the 23 bits reversed (stock 0x8009920 / 0x800994C). DCS 023 gives 0x763813. */
static uint32_t dcs_codeword(uint32_t code)
{
    uint32_t x = code + 0x800u, g = 0, r, c = 0;
    for (int i = 0; i < 12; i++) { g = (g << 1) | (x & 1u); x >>= 1; }
    g <<= 11;
    r = g;
    for (int i = 0; i < 12; i++)
        if (r >> (22 - i)) r ^= 0xAE3u << (11 - i);
    r += g;
    for (int i = 0; i < 23; i++) { c = (c << 1) | (r & 1u); r >>= 1; }
    return c & 0x7FFFFFu;
}

/* Tone on transmit (stock 0x8004388). CTCSS: 0x51 = 0x9041, 0x07 = tone (0.1 Hz) * 0x19D / 200.
 * DCS: 0x51 = 0x802C (normal) or 0xA02C (inverted), 0x07 = 0x0AD7, 0x08 = low 12 bits, then
 * 0x08 = high bits + 0x8000. */
static void tone_set(int type, uint16_t val)
{
    if (type == TONE_CTCSS) {
        bk1_write(0x51, 0x9041);
        bk1_write(0x07, (uint16_t)(((uint32_t)val * 0x19Du / 200u) & 0x1FFF));
    } else if (type == TONE_DCS_N || type == TONE_DCS_I) {
        uint32_t cw = dcs_codeword(val);
        bk1_write(0x51, type == TONE_DCS_I ? 0xA02C : 0x802C);
        bk1_write(0x07, 0x0AD7);
        bk1_write(0x08, (uint16_t)(cw & 0xFFFu));
        bk1_write(0x08, (uint16_t)((cw >> 12) + 0x8000u));
    } else {
        bk1_write(0x51, 0x0000);
    }
}
static void tx_tone(void) { tone_set(tone_type, tone_val); }
static void rx_tone(void) { tone_set(rx_tone_type, rx_tone_val); }

/* Switch the transmit audio between the mic and the 1750 Hz burst (tone path as in test 7). */
static void tx_audio(int burst)
{
    if (burst) {
        bk1_write(0x71, TONE_1750);
        bk1_write(0x70, 0xC000);                                 /* tone 1 on */
        bk1_write(0x47, 0x6040 | (3 << 8));                      /* AF: tone */
        bk1_write(0x30, 0xC3FA);
    } else {
        bk1_write(0x30, 0xC1FE);
        bk1_write(0x47, 0x6040 | (1 << 8));                      /* AF: FM (microphone) */
        bk1_write(0x70, 0x0000);
        bk1_write(0x71, 0x68DC);
    }
    tx_burst = burst;
}

static int tx_start(int burst)
{
    uint32_t tf = tx_freq();
    const band_t *b = band_of(tf);
    int allow = tx_allowed(tf);
    if (!b || !allow) return 0;
    tx_f = tf;
    tx_power = power < allow - 1 ? power : allow - 1;
    audio_close();
    gpio_bits_reset(GPIOA, GPIO_PINS_13);
    gpio_bits_set(GPIOA, GPIO_PINS_14);
    bk1_write(0x31, bk_read(0, 0x31) & ~0x000Au);                 /* scrambler and compander off */
    bk1_write(0x28, 0x6B38);
    bk1_write(0x29, 0xAB40);
    bk1_write(0x33, bk_read(0, 0x33) & 0xFFC0);                 /* RX filters and TX amps off */
    bk1_write(0x47, 0x6040 | (1 << 8));                          /* AF: FM (microphone) */
    if (b->tx_var == 3 || b->tx_var == 4) gpio_bits_reset(GPIOF, GPIO_PINS_0);
    else gpio_bits_set(GPIOF, GPIO_PINS_0);
    {   /* TX frequency (differs from receive when a repeater shift is set) */
        uint32_t v = tf + cal_word(tf, b->cal) - 0x8000u;
        bk1_write(0x38, (uint16_t)(v & 0xFFFF));
        bk1_write(0x39, (uint16_t)(v >> 16));
    }
    tx_tone();
    bk1_write(0x71, 0x68DC);                                     /* scrambler off */
    bk1_write(0x70, 0x0000);                                     /* no tone */
    bk1_write(0x74, cal_ok ? af3k[cal_hdr[9]] : 0xE61C);          /* TX 3 kHz response */
    bk1_write(0x7D, (uint16_t)(0xE940 + mic_gain));
    bk1_write(0x40, cal_dev(tf, mode == MODE_NFM));
    bk1_write(0x37, 0x1D0F);
    delay_ms(10);
    bk1_write(0x52, 0x028F);
    bk1_write(0x30, 0x0200);
    bk1_write(0x30, 0xC1FE);
    tx_burst = 0;
    if (burst) tx_audio(1);
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    uint16_t p = power_reg(tf, tx_power);
    if (p) {
        /* Band amplifier (stock 0x8005354): 1 PC15; 2 PC15 + PB11; 3 0x33 bit 3; 4, 5 0x33 bit 2. */
        switch (b->tx_var) {
        case 1: gpio_bits_set(GPIOC, GPIO_PINS_15); break;
        case 2: gpio_bits_set(GPIOC, GPIO_PINS_15); gpio_bits_set(GPIOB, GPIO_PINS_11); break;
        case 3: bk1_write(0x33, bk_read(0, 0x33) | 0x0008); break;
        default: bk1_write(0x33, bk_read(0, 0x33) | 0x0004); break;
        }
        bk1_write(0x36, p);
    } else {
        bk1_write(0x36, TX_POWER_MIN);
    }
    tx_on = 1;
    return 1;
}

static void tx_stop(void)
{
    bk1_write(0x36, 0x007F);
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    bk1_write(0x33, bk_read(0, 0x33) & 0xFFC0);
    bk1_write(0x70, 0x0000);
    bk1_write(0x71, 0x68DC);
    bk1_write(0x51, 0x0000);
    bk1_write(0x30, 0x0200);
    tx_on = 0;
    tx_burst = 0;
    gpio_bits_reset(GPIOA, GPIO_PINS_14);
    radio_mode();
    radio_tune(freq10);
}

/* ---------------- keys ---------------- */
/* Matrix: columns PB0 PB1 PB2 PB10 driven low in turn; rows PC10 PC11 PC12 PD2.
 * Bit (col * 4 + row). Map from presses on this radio (FINDINGS.md). */
static const uint16_t col_pin[4] = { GPIO_PINS_0, GPIO_PINS_1, GPIO_PINS_2, GPIO_PINS_10 };
#define COLS_ALL (GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_2 | GPIO_PINS_10)
enum { K_MENU = 'M', K_UP = 'U', K_DOWN = 'D', K_EXIT = 'X' };
static const char key_char[16] = {
    K_MENU, K_UP, K_DOWN, K_EXIT,    /* col 0 */
    '3', '6', '9', '#',              /* col 1 */
    '2', '5', '8', '0',              /* col 2 */
    '1', '4', '7', '*',              /* col 3 */
};

static void keys_init(void)
{
    gpio_bits_set(GPIOB, COLS_ALL);
    pin_out(GPIOB, COLS_ALL);
    pin_in(GPIOC, GPIO_PINS_10 | GPIO_PINS_11 | GPIO_PINS_12, GPIO_PULL_UP);
    pin_in(GPIOD, GPIO_PINS_2, GPIO_PULL_UP);
    pin_in(GPIOB, GPIO_PINS_6, GPIO_PULL_UP);                      /* PTT1 */
}

static uint16_t keys_matrix(void)
{
    uint16_t m = 0;
    for (int c = 0; c < 4; c++) {
        gpio_bits_set(GPIOB, COLS_ALL);
        gpio_bits_reset(GPIOB, col_pin[c]);
        delay_short();
        if (gpio_input_data_bit_read(GPIOC, GPIO_PINS_10) == RESET) m |= 1u << (c * 4 + 0);
        if (gpio_input_data_bit_read(GPIOC, GPIO_PINS_11) == RESET) m |= 1u << (c * 4 + 1);
        if (gpio_input_data_bit_read(GPIOC, GPIO_PINS_12) == RESET) m |= 1u << (c * 4 + 2);
        if (gpio_input_data_bit_read(GPIOD, GPIO_PINS_2)  == RESET) m |= 1u << (c * 4 + 3);
    }
    gpio_bits_set(GPIOB, COLS_ALL);
    return m;
}

static int ptt_down(void) { return gpio_input_data_bit_read(GPIOB, GPIO_PINS_6) == RESET; }

/* ---------------- screen ---------------- */
static void fmt_freq(char *out, uint32_t f)   /* "MMMM.mmmmm" with leading spaces */
{
    uint32_t mhz = f / 100000, frac = f % 100000;
    char t[16];
    int n = 0;
    do { t[n++] = (char)('0' + mhz % 10); mhz /= 10; } while (mhz);
    int p = 0;
    while (n) out[p++] = t[--n];
    out[p++] = '.';
    for (uint32_t d = 10000; d; d /= 10) out[p++] = (char)('0' + (frac / d) % 10);
    out[p] = 0;
}

/* Tone as text: "T88.5" for CTCSS, "D023N" / "D023I" for DCS, "" when off. */
static int tone_text_of(int type, uint16_t val, char *s)
{
    int p = 0;
    if (type == TONE_CTCSS) {
        s[p++] = 'T';
        if (val >= 1000) s[p++] = (char)('0' + val / 1000);
        s[p++] = (char)('0' + val / 100 % 10);
        s[p++] = (char)('0' + val / 10 % 10);
        s[p++] = '.';
        s[p++] = (char)('0' + val % 10);
    } else if (type == TONE_DCS_N || type == TONE_DCS_I) {
        s[p++] = 'D';
        s[p++] = (char)('0' + (val >> 6 & 7));
        s[p++] = (char)('0' + (val >> 3 & 7));
        s[p++] = (char)('0' + (val & 7));
        s[p++] = type == TONE_DCS_I ? 'I' : 'N';
    }
    s[p] = 0;
    return p;
}
static int tone_text(char *s) { return tone_text_of(tone_type, tone_val, s); }

/* ---------------- home screen (240 x 320) ---------------- */
static int menu_open;                 /* while the menu is shown, home-screen drawing waits */
static int vfo_cur;                   /* 0 = VFO A, 1 = VFO B */
static const char *const step_name[] = { "100 Hz", "1 kHz", "2.5 kHz", "5 kHz", "6.25 kHz", "10 kHz", "12.5 kHz", "25 kHz", "50 kHz", "100 kHz" };
#define Y_FREQ    42
#define Y_CHIPS1  104
#define Y_CHIPS2  140
#define Y_SEP1    178
#define Y_STATUS  186
#define STATUS_H  38
#define Y_INFO    230
#define Y_SEP2    252
#define Y_MLABEL  256
#define Y_METER   278
#define METER_H   18
#define Y_MSCALE  299
#define Y_SOFT    296      /* labels over the green and red bars (menu only) */
#define C_EDIT    RGB(0, 210, 0)
#define CHIP_H    30

static void separator(int y)
{
    lcd_fill(14, y, LCD_W - 28, 1, P->sep);
}

/* Receive bands (menu item 1): each remembers its last frequency; mode, step and the usual repeater
 * offset (Region 2) are set on entry, and the shift is switched off. */
typedef struct { const char *name; uint32_t lo, hi, start; uint8_t mode, step, ofs; } rxband_t;
static const rxband_t rxbands[] = {
    { "10 m",    2800000,   2970000,   2840000, MODE_USB, 1, 0 },
    { "6 m",     5000000,   5400000,   5015000, MODE_USB, 1, 3 },
    { "AIR",    11800000,  13700000,  11800000, MODE_AM,  7, 2 },
    { "2 m",    14400000,  14800000,  14550000, MODE_NFM, 6, 2 },
    { "MARINE", 15600000,  16202500,  15680000, MODE_FM,  7, 2 },
    { "220",    22000000,  22500000,  22350000, MODE_NFM, 6, 4 },
    { "70 cm",  42000000,  45000000,  43350000, MODE_NFM, 6, 5 },
    { "23 cm", 124000000, 130000000, 129750000, MODE_NFM, 7, 8 },
};
#define NRXBANDS ((int)(sizeof rxbands / sizeof rxbands[0]))
static uint32_t band_freq[NRXBANDS];
static int band_last = 3;

static int rxband_of(uint32_t f)
{
    for (int i = 0; i < NRXBANDS; i++) if (f >= rxbands[i].lo && f <= rxbands[i].hi) return i;
    return -1;
}

static void top_label(void)
{
    if (menu_open) return;
    int b = rxband_of(freq10);
    char s[16] = { vfo_cur ? 'B' : 'A', ' ', ' ' };
    int k = 3;
    for (const char *q = b >= 0 ? rxbands[b].name : "VFO"; *q; q++) s[k++] = *q;
    s[k] = 0;
    lcd_fill(0, 4, 120, 26, P->bar);
    aa_text(10, (34 - F_SMALL->height) / 2, F_SMALL, s, P->light, P->bar);
}

static void top_bar(void)
{
    lcd_fill(0, 0, LCD_W, 34, P->bar);
    separator(Y_SEP1);
    separator(Y_SEP2);
}

/* Battery from PA7 with the stock formula ADC * 3 / 51 + 2 (0.1 V), gauge 6.8-8.4 V. */
static int batt_v = 84;               /* 0.1 V */
static void show_batt(void)
{
    int v = (int)adc_read(ADC_CHANNEL_7) * 3 / 51 + 2;
    batt_v = v;
    char s[8];
    s[0] = (char)('0' + v / 10 % 10); s[1] = '.'; s[2] = (char)('0' + v % 10); s[3] = 'V'; s[4] = 0;
    int ix = LCD_W - 40, iy = 11;
    lcd_fill(ix - 52, 4, 52, 26, P->bar);
    aa_text(ix - 6 - aa_width(F_SMALL, s), (34 - F_SMALL->height) / 2, F_SMALL, s, P->light, P->bar);
    lcd_fill(ix, iy, 26, 12, P->light);
    lcd_fill(ix + 2, iy + 2, 22, 8, P->bar);
    lcd_fill(ix + 26, iy + 3, 3, 6, P->light);
    int fill = (v - 68) * 20 / 16;
    if (fill < 0) fill = 0;
    if (fill > 20) fill = 20;
    if (fill) lcd_fill(ix + 3, iy + 3, fill, 6, v <= 72 ? C_RED : C_GREEN);
}

/* A row of rounded labels, centred. */
#define CHIP_PAD 8
#define CHIP_GAP 8
static void chip_row(int y, const char *const *txt, const uint16_t *bg, int n)
{
    int total = 0;
    for (int k = 0; k < n; k++) total += aa_width(F_SMALL, txt[k]) + 2 * CHIP_PAD + (k ? CHIP_GAP : 0);
    lcd_fill(0, y, LCD_W, CHIP_H, C_BLACK);
    int x = (LCD_W - total) / 2;
    for (int k = 0; k < n; k++) {
        int w = aa_width(F_SMALL, txt[k]) + 2 * CHIP_PAD;
        fill_rrect(x, y, w, CHIP_H, bg[k]);
        aa_text(x + CHIP_PAD, y + (CHIP_H - F_SMALL->height) / 2, F_SMALL, txt[k], P->chip_text, bg[k]);
        x += w + CHIP_GAP;
    }
}

/* "TX 88.5", "RX D023N" */
static int tone_chip_text(char *s, const char *pre, int type, uint16_t val)
{
    char t[12];
    if (!tone_text_of(type, val, t)) return 0;
    int p = 0;
    for (; *pre; pre++) s[p++] = *pre;
    for (const char *q = t[0] == 'T' ? t + 1 : t; *q; q++) s[p++] = *q;
    s[p] = 0;
    return 1;
}

static void show_chips(void)
{
    if (menu_open) return;
    char mic[8] = "MIC ", tx[16], rx[16];
    mic[4] = (char)('0' + mic_gain / 10); mic[5] = (char)('0' + mic_gain % 10); mic[6] = 0;
    char sh[8];
    if (shift) { sh[0] = shift > 0 ? '+' : '-'; int k = 1; for (const char *q = ofs_name[ofs_idx]; *q; q++) sh[k++] = *q; sh[k] = 0; }
    const char *r1[3] = { mode_name[mode], power_name[power], shift ? sh : mic };
    const uint16_t c1[3] = { P->mode, P->pw[power], shift ? RGB(255, 190, 0) : P->mic };
    chip_row(Y_CHIPS1, r1, c1, 3);
    const char *r2[2];
    uint16_t c2[2];
    int n = 0;
    if (tone_chip_text(tx, "TX ", tone_type, tone_val)) { r2[n] = tx; c2[n++] = P->txt; }
    if (tone_chip_text(rx, "RX ", rx_tone_type, rx_tone_val)) { r2[n] = rx; c2[n++] = P->rxt; }
    chip_row(Y_CHIPS2, r2, c2, n);
}

static void show_info(void)
{
    if (menu_open) return;
    char s[24] = "SQ ";
    int p = 3;
    s[p++] = (char)('0' + sq_level);
    for (const char *q = "    STEP "; *q; q++) s[p++] = *q;
    for (const char *q = step_name[step_idx]; *q; q++) s[p++] = *q;
    s[p] = 0;
    lcd_fill(0, Y_INFO, LCD_W, F_SMALL->height, C_BLACK);
    aa_text_c(Y_INFO, F_SMALL, s, P->dim, C_BLACK);
}

/* "446.006" large, "25" smaller on the same baseline, centred. */
static void show_freq(void)
{
    if (menu_open) return;
    char s[16], big[12], small[4];
    fmt_freq(s, freq10);
    int dot = 0;
    while (s[dot] && s[dot] != '.') dot++;
    int i = 0;
    for (; i < dot + 4 && s[i]; i++) big[i] = s[i];
    big[i] = 0;
    int j = 0;
    for (; s[i] && j < 2; i++) small[j++] = s[i];
    small[j] = 0;
    int wb = aa_width(&font_big, big), ws = aa_width(F_MED, small) + 4;
    if (wb + ws > LCD_W - 8) ws = 0;
    int bi = rxband_of(freq10);
    if (bi >= 0) { band_freq[bi] = freq10; band_last = bi; }
    top_label();
    int x0 = (LCD_W - wb - ws) / 2;
    lcd_fill(0, Y_FREQ, x0, font_big.height, C_BLACK);
    int x = aa_text(x0, Y_FREQ, &font_big, big, C_WHITE, C_BLACK);
    if (ws) {
        int ys = Y_FREQ + font_big.baseline - F_MED->baseline;
        lcd_fill(x, Y_FREQ, 4, font_big.height, C_BLACK);
        lcd_fill(x + 4, Y_FREQ, ws - 4, ys - Y_FREQ, C_BLACK);
        x = aa_text(x + 4, ys, F_MED, small, P->light, C_BLACK);
        if (ys + F_MED->height < Y_FREQ + font_big.height)
            lcd_fill(x0 + wb, ys + F_MED->height, ws, Y_FREQ + font_big.height - ys - F_MED->height, C_BLACK);
    }
    if (x < LCD_W) lcd_fill(x, Y_FREQ, LCD_W - x, font_big.height, C_BLACK);
    show_chips();
}

/* Signal meter at the bottom: 20 segments, 12 up to S9 (4.5 dB each), 8 above S9 (5 dB each, to
 * S9+40), white up to S9 and red above. S9 = -93 dBm above 30 MHz, -73 dBm below (IARU). Only changed
 * segments are redrawn. */
#define NSEG 20
#define SEG_W 9
#define SEG_PITCH 11
#define SEG_X ((LCD_W - (NSEG - 1) * SEG_PITCH - SEG_W) / 2)
static int meter_bars = -1;
static int meter_label_force = 1;


/* Scale under the meter, CB style: white 1-9, red above S9. Segment n ends at S-unit n * 4 / 3. */
static void meter_scale(void)
{
    static const char *const lab[] = { "1", "3", "5", "7", "9", "+20", "+40" };
    static const uint8_t pos3[] = { 4, 12, 20, 28, 36, 48, 60 };   /* boundary in thirds of a segment */
    lcd_fill(0, Y_MSCALE, LCD_W, F_SMALL->height, C_BLACK);
    for (unsigned k = 0; k < sizeof pos3; k++) {
        int cx = SEG_X + pos3[k] * SEG_PITCH / 3 - 1;
        int w = aa_width(F_SMALL, lab[k]);
        int x = cx - w / 2;
        if (x < 0) x = 0;
        if (x + w > LCD_W) x = LCD_W - w;
        aa_text(x, Y_MSCALE, F_SMALL, lab[k], k < 5 ? C_WHITE : RGB(255, 40, 40), C_BLACK);
    }
}

static void show_rssi(int dbm, int noise)
{
    (void)noise;
    if (menu_open) return;
    int s9 = freq10 < 3000000 ? -73 : -93;
    int rel = dbm - (s9 - 54);                    /* dB above S0 */
    int bars = rel <= 0 ? 0 : rel < 54 ? rel * 2 / 9 : 12 + (rel - 54) / 5;
    if (bars > NSEG) bars = NSEG;
    for (int k = 0; k < NSEG; k++) {
        int on = k < bars;
        if (meter_bars >= 0 && on == (k < meter_bars)) continue;
        uint16_t col = !on ? C_SEGOFF : k < 12 ? C_WHITE : RGB(255, 40, 40);
        lcd_fill(SEG_X + k * SEG_PITCH, Y_METER, SEG_W, METER_H, col);
    }
    meter_bars = bars;
    /* "S7  -105 dBm", "S9+20  -73 dBm" */
    char s[24];
    int p = 0;
    s[p++] = 'S';
    if (rel < 54) {
        int su = rel / 6;
        if (su < 0) su = 0;
        s[p++] = (char)('0' + su);
    } else {
        s[p++] = '9';
        int over = (rel - 54) / 10 * 10;
        if (over) { s[p++] = '+'; if (over >= 10) s[p++] = (char)('0' + over / 10 % 10); s[p++] = '0'; }
    }
    s[p++] = ' '; s[p++] = ' ';
    int v = dbm < 0 ? -dbm : dbm;
    if (dbm < 0) s[p++] = '-';
    char d[4]; int n = 0;
    do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) s[p++] = d[--n];
    s[p++] = ' '; s[p++] = 'd'; s[p++] = 'B'; s[p++] = 'm'; s[p] = 0;
    /* Redraw only when the text changes, and only clear the margins, so it does not flicker. */
    static char shown[24];
    if (!meter_label_force && !__builtin_strcmp(s, shown)) return;
    meter_label_force = 0;
    for (int k = 0; k <= p; k++) shown[k] = s[k];
    int w = aa_width(F_SMALL, s), x = (LCD_W - w) / 2;
    lcd_fill(0, Y_MLABEL, x, F_SMALL->height, C_BLACK);
    lcd_fill(x + w, Y_MLABEL, LCD_W - x - w, F_SMALL->height, C_BLACK);
    aa_text(x, Y_MLABEL, F_SMALL, s, P->light, C_BLACK);
}

/* Status banner; remembers the last text so the frequency entry can borrow the banner. */
static const char *status_text = "RX";
static uint16_t status_fg = C_GREEN;
static void banner(const char *s, uint16_t bg)
{
    fill_rrect(8, Y_STATUS, LCD_W - 16, STATUS_H, bg);
    aa_text_c(Y_STATUS + (STATUS_H - F_MED->height) / 2, F_MED, s, C_WHITE, bg);
}

static void show_status(const char *s, uint16_t fg)
{
    status_text = s;
    status_fg = fg;
    if (menu_open) return;
    banner(s, fg == C_RED ? P->b_tx : fg == C_YELLOW ? P->b_warn : P->b_rx);
}

/* "TX LOW", "TX LOW 1750" */
static void show_tx(void)
{
    static char s[20];
    int p = 0;
    s[p++] = 'T'; s[p++] = 'X'; s[p++] = ' ';
    for (const char *n = power_name[tx_power]; *n; n++) s[p++] = *n;
    if (tx_burst) { s[p++] = ' '; s[p++] = '1'; s[p++] = '7'; s[p++] = '5'; s[p++] = '0'; }
    else if (tx_f != freq10) {
        char f[16];
        fmt_freq(f, tx_f);
        s[p++] = ' ';
        int k = 0, dot = -1;
        for (; f[k] && (dot < 0 || k <= dot + 3); k++) { if (f[k] == '.') dot = k; s[p++] = f[k]; }
    }
    s[p] = 0;
    show_status(s, C_RED);
}

/* Frequency being typed, shown in the banner; empty restores the status. */
static void show_entry(const char *e)
{
    if (menu_open) return;
    if (!*e) { show_status(status_text, status_fg); return; }
    char s[20];
    int p = 0;
    for (; *e && p < 10; e++) s[p++] = *e;
    s[p++] = ' '; s[p++] = 'k'; s[p++] = 'H'; s[p++] = 'z'; s[p] = 0;
    banner(s, P->b_entry);
}

/* Redraw everything after a style change. */
/* Labels over the keys under the screen: green bar (left) and red bar (right). */
static void soft_keys(const char *left, const char *right)
{
    lcd_fill(0, Y_SOFT, LCD_W, LCD_H - Y_SOFT, C_BLACK);
    int wl = aa_width(F_SMALL, left) + 24, wr = right ? aa_width(F_SMALL, right) + 24 : 0, h = LCD_H - Y_SOFT - 2;
    fill_rrect(6, Y_SOFT, wl, h, C_EDIT);
    aa_text(6 + 12, Y_SOFT + (h - F_SMALL->height) / 2, F_SMALL, left, C_BLACK, C_EDIT);
    if (!right) return;
    fill_rrect(LCD_W - 6 - wr, Y_SOFT, wr, h, RGB(230, 0, 0));
    aa_text(LCD_W - 6 - wr + 12, Y_SOFT + (h - F_SMALL->height) / 2, F_SMALL, right, C_WHITE, RGB(230, 0, 0));
}

static void clear_band(int y0, int y1)
{
    if (y1 > y0) lcd_fill(0, y0, LCD_W, y1 - y0, C_BLACK);
}

static void draw_home(void)
{
    /* Clear only the bands between sections; each section paints its own area. */
    clear_band(34, Y_FREQ);
    clear_band(Y_FREQ + font_big.height, Y_CHIPS1);
    clear_band(Y_CHIPS1 + CHIP_H, Y_CHIPS2);
    clear_band(Y_CHIPS2 + CHIP_H, Y_STATUS);
    lcd_fill(0, Y_STATUS, 8, STATUS_H, C_BLACK);
    lcd_fill(LCD_W - 8, Y_STATUS, 8, STATUS_H, C_BLACK);
    lcd_fill(8, Y_STATUS, LCD_W - 16, 4, C_BLACK);
    lcd_fill(8, Y_STATUS + STATUS_H - 4, LCD_W - 16, 4, C_BLACK);
    clear_band(Y_STATUS + STATUS_H, Y_INFO);
    clear_band(Y_INFO + F_SMALL->height, Y_MLABEL);
    clear_band(Y_MLABEL + F_SMALL->height, Y_METER);
    clear_band(Y_METER, Y_METER + METER_H);
    clear_band(Y_METER + METER_H, Y_MSCALE);
    clear_band(Y_MSCALE + F_SMALL->height, LCD_H);
    top_bar();
    show_freq();
    show_status(status_text, status_fg);
    show_info();
    show_batt();
    meter_scale();
    meter_bars = -1;
    meter_label_force = 1;
}

/* Simple AGC for USB and AM (stock does its own, 0x8024140): register 0x13 gain steps, PGA (bits
 * 2:0) first, then LNA (bits 7:5). Steps down above -60 dBm, back up below -80 dBm, 300 ms apart. */
static const uint16_t agc_steps[] = { 0x03BE, 0x03BD, 0x03BC, 0x03BB, 0x03BA, 0x03B9, 0x03B8,
                                      0x0398, 0x0378, 0x0358, 0x0338 };
#define NAGC ((int)(sizeof agc_steps / sizeof agc_steps[0]))
static int agc_on = 1, agc_idx;
static uint32_t agc_next;
static void agc_run(int dbm)
{
    int want = agc_idx;
    if (!agc_on || (mode != MODE_USB && mode != MODE_AM)) want = 0;
    else if ((int32_t)(ticks - agc_next) >= 0) {
        if (dbm > -60 && agc_idx < NAGC - 1) want = agc_idx + 1;
        else if (dbm < -80 && agc_idx > 0) want = agc_idx - 1;
    }
    if (want != agc_idx) {
        agc_idx = want;
        bk1_write(0x13, agc_steps[agc_idx]);
        agc_next = ticks + 300;
    }
}

/* ---------------- VFO A / B ---------------- */
/* Each VFO keeps its frequency, mode, step, repeater shift and offset, and tones; * swaps them. */
typedef struct { uint32_t freq; uint8_t mode, step, ofs; int8_t shift; uint8_t tt, rt; uint16_t tv, rv; } vfo_t;
static vfo_t vfos[2] = {
    { 14550000, MODE_NFM, 6, 2, 0, TONE_OFF, TONE_OFF, 0, 0 },
    { 43350000, MODE_NFM, 6, 5, 0, TONE_OFF, TONE_OFF, 0, 0 },
};

static void vfo_select(int n)
{
    vfo_t *v = &vfos[vfo_cur];
    v->freq = freq10; v->mode = (uint8_t)mode; v->step = (uint8_t)step_idx; v->ofs = (uint8_t)ofs_idx;
    v->shift = (int8_t)shift; v->tt = (uint8_t)tone_type; v->tv = tone_val; v->rt = (uint8_t)rx_tone_type; v->rv = rx_tone_val;
    vfo_cur = n;
    v = &vfos[vfo_cur];
    mode = v->mode; step_idx = v->step; ofs_idx = v->ofs; shift = v->shift;
    tone_type = v->tt; tone_val = v->tv; rx_tone_type = v->rt; rx_tone_val = v->rv;
    audio_close();
    radio_tune(v->freq);
    radio_mode();
}

/* ---------------- backlight timeout, scan ---------------- */
static void wake(void)
{
    last_activity = ticks;
    if (!lit) {
        lit = 1;
        backlight(bl_list[bl_idx]);
        gpio_bits_set(GPIOA, GPIO_PINS_9);
    }
}

/* Scan the current receive band by the menu step; stop on a signal, carry on 3 s after it goes. */
static void scan_start(void)
{
    if (rxband_of(freq10) < 0) { show_status("SCAN: NOT IN A BAND", C_YELLOW); return; }
    scanning = 1;
    scan_next = ticks;
    scan_hold_until = ticks;
    show_status("SCANNING", C_GREEN);
}

static void scan_stop(void)
{
    if (!scanning) return;
    scanning = 0;
    show_status("RX", C_GREEN);
}

static void scan_step(void)
{
    if (audio_on) { scan_hold_until = ticks + 3000; return; }
    if ((int32_t)(ticks - scan_hold_until) < 0 || (int32_t)(ticks - scan_next) < 0) return;
    int b = rxband_of(freq10);
    if (b < 0) { scan_stop(); return; }
    uint32_t f = freq10 + step_list[step_idx];
    if (f > rxbands[b].hi) f = rxbands[b].lo;
    radio_tune(f);
    show_freq();
    if (status_text[0] != 'S') show_status("SCANNING", C_GREEN);
    scan_next = ticks + 150;
}

static void report_freq(void);

/* Spectrum: 44 points around the current frequency at the menu step, signal strength as bars. */
#define SPEC_N   44
#define SPEC_TOP 104
#define SPEC_H   136
static void spectrum_start(void)
{
    audio_close();
    spectrum_on = 1;
    lcd_fill(0, SPEC_TOP, LCD_W, Y_MSCALE + F_SMALL->height - SPEC_TOP, C_BLACK);
    char s[32] = "SPECTRUM, STEP ";
    int p = 15;
    for (const char *q = step_name[step_idx]; *q; q++) s[p++] = *q;
    s[p] = 0;
    aa_text_c(SPEC_TOP + SPEC_H + 8, F_SMALL, s, P->dim, C_BLACK);
}

static void spectrum_sweep(void)
{
    uint32_t center = freq10, st = step_list[step_idx];
    for (int i = 0; i < SPEC_N; i++) {
        wdt_counter_reload();
        uint32_t f = center + (uint32_t)(i - SPEC_N / 2) * st;
        int h = 0;
        if (radio_tune(f)) {
            delay_ms(2);
            h = (rssi_dbm() + 125) * SPEC_H / 100;
            if (h < 1) h = 1;
            if (h > SPEC_H) h = SPEC_H;
        }
        int x = 10 + i * 5;
        lcd_fill(x, SPEC_TOP, 4, SPEC_H - h, C_BLACK);
        lcd_fill(x, SPEC_TOP + SPEC_H - h, 4, h, i == SPEC_N / 2 ? C_YELLOW : C_WHITE);
    }
    radio_tune(center);
}

static void spectrum_stop(void)
{
    spectrum_on = 0;
    radio_tune(freq10);
    draw_home();
}

/* Frequency detect (stock 0x8004B70 / 0x8004B24): the chip's frequency scan, 0x32 = 0x0B01; when 0x0D
 * bit 15 is clear, (0x0D & 0x7FF) << 16 | 0x0E + 0x8028 - calibration word is the frequency found. */
static void fdet_start(void)
{
    audio_close();
    bk1_write(0x32, 0x0B01);
    fdet_on = 1;
    fdet_next = ticks + 200;
    fdet_until = ticks + 20000;
    show_status("DETECTING", C_GREEN);
}

static void fdet_stop(void)
{
    bk1_write(0x32, 0x0000);
    fdet_on = 0;
    radio_tune(freq10);
}

static void fdet_poll(void)
{
    if ((int32_t)(ticks - fdet_next) < 0) return;
    fdet_next = ticks + 200;
    uint16_t r0d = bk_read(0, 0x0D);
    if (!(r0d & 0x8000)) {
        uint32_t raw = ((uint32_t)(r0d & 0x7FF) << 16) | bk_read(0, 0x0E);
        const band_t *b = band_of(freq10);
        uint32_t f = raw + 0x8028u - cal_word(freq10, b ? b->cal : 0x8000);
        f = (f + 50) / 100 * 100;                      /* nearest 1 kHz */
        fdet_stop();
        if (radio_tune(f)) { show_freq(); show_status("FOUND", C_GREEN); report_freq(); }
        else show_status("FOUND, OUT OF RANGE", C_YELLOW);
    } else if ((int32_t)(ticks - fdet_until) >= 0) {
        fdet_stop();
        show_status("NOTHING FOUND", C_YELLOW);
    }
}

/* ---------------- menu ---------------- */
/* Green bar opens it; up/down picks an item; green edits; up/down changes the value (applied at once,
 * up/down repeat when held); # jumps to the next tone type; green keeps the value, red bar restores
 * it; red bar leaves the menu. Keys 1-9, 0, * and # open items 1-12 for editing. PTT closes the menu. */
static const uint16_t ctcss_list[50] = {
    670, 693, 719, 744, 770, 797, 825, 854, 885, 915, 948, 974, 1000, 1035, 1072, 1109, 1148,
    1188, 1230, 1273, 1318, 1365, 1413, 1462, 1514, 1567, 1598, 1622, 1655, 1679, 1713, 1738,
    1773, 1799, 1835, 1862, 1899, 1928, 1966, 1995, 2035, 2065, 2107, 2181, 2257, 2291, 2336,
    2418, 2503, 2541 };
static const uint16_t dcs_list[105] = {   /* octal codes */
    0023, 0025, 0026, 0031, 0032, 0036, 0043, 0047, 0051, 0053, 0054, 0065, 0071, 0072, 0073,
    0074, 0114, 0115, 0116, 0122, 0125, 0131, 0132, 0134, 0143, 0145, 0152, 0155, 0156, 0162,
    0165, 0172, 0174, 0205, 0212, 0223, 0225, 0226, 0243, 0244, 0245, 0246, 0251, 0252, 0255,
    0261, 0263, 0265, 0266, 0271, 0274, 0306, 0311, 0315, 0325, 0331, 0332, 0343, 0346, 0351,
    0356, 0364, 0365, 0371, 0411, 0412, 0413, 0423, 0431, 0432, 0445, 0446, 0452, 0454, 0455,
    0462, 0464, 0465, 0466, 0503, 0506, 0516, 0523, 0526, 0532, 0546, 0565, 0606, 0612, 0624,
    0627, 0631, 0632, 0645, 0654, 0662, 0664, 0703, 0712, 0723, 0731, 0732, 0734, 0743, 0754 };
#define NTONES (1 + 50 + 105 + 105)        /* off, CTCSS, DCS normal, DCS inverted */

static int tone_index(int type, uint16_t val)
{
    if (type == TONE_CTCSS) { for (int i = 0; i < 50; i++) if (ctcss_list[i] == val) return 1 + i; return 1; }
    if (type == TONE_DCS_N || type == TONE_DCS_I) {
        int base = type == TONE_DCS_N ? 51 : 156;
        for (int i = 0; i < 105; i++) if (dcs_list[i] == val) return base + i;
        return base;
    }
    return 0;
}

static void tone_from_index(int i, int *type, uint16_t *val)
{
    if (i <= 0) { *type = TONE_OFF; *val = 0; }
    else if (i <= 50) { *type = TONE_CTCSS; *val = ctcss_list[i - 1]; }
    else if (i <= 155) { *type = TONE_DCS_N; *val = dcs_list[i - 51]; }
    else { *type = TONE_DCS_I; *val = dcs_list[i - 156]; }
}

enum { MI_BAND, MI_POWER, MI_TXTONE, MI_RXTONE, MI_SHIFT, MI_OFFSET, MI_SQ, MI_STEP, MI_MODE, MI_MIC, MI_TOT, MI_BL,
       MI_LIGHT, MI_SCAN, MI_SPECTRUM, MI_FDET, MI_COUNT };
static const char *const menu_name[MI_COUNT] = {
    "Band", "Power", "TX tone", "RX tone", "Shift", "Offset", "Squelch", "Step", "Mode", "Mic gain", "Time-out", "Backlight",
    "Light off", "Scan", "Spectrum", "Freq detect" };
static const char menu_key_of[MI_COUNT] = { '1', '2', '3', '4', '5', '6', '7', '8', '9', '0', '*', '#', ' ', ' ', ' ', ' ' };
static int menu_sel, menu_edit;

static void put2(char *s, int v) { if (v >= 10) *s++ = (char)('0' + v / 10); *s++ = (char)('0' + v % 10); *s = 0; }

static void menu_value(int item, char *s)
{
    const char *src = 0;
    switch (item) {
    case MI_BAND: { int b = rxband_of(freq10); src = b >= 0 ? rxbands[b].name : "VFO"; break; }
    case MI_POWER: src = power_name[power]; break;
    case MI_SHIFT: src = shift > 0 ? "+" : shift < 0 ? "-" : "OFF"; break;
    case MI_OFFSET: src = ofs_name[ofs_idx]; break;
    case MI_MIC: put2(s, mic_gain); return;
    case MI_TXTONE: case MI_RXTONE: {
        char t[12];
        if (!(item == MI_TXTONE ? tone_text(t) : tone_text_of(rx_tone_type, rx_tone_val, t))) { src = "OFF"; break; }
        const char *q = t[0] == 'T' ? t + 1 : t;
        while ((*s++ = *q++)) { }
        return;
    }
    case MI_SQ: if (!sq_level) { src = "OPEN"; break; } put2(s, sq_level); return;
    case MI_STEP: src = step_name[step_idx]; break;
    case MI_MODE: src = mode_name[mode]; break;
    case MI_TOT: { int v = tot_list[tot_idx], p = 0; char d[4]; int n = 0;
                   do { d[n++] = (char)('0' + v % 10); v /= 10; } while (v);
                   while (n) s[p++] = d[--n];
                   s[p++] = ' '; s[p++] = 's'; s[p] = 0; return; }
    case MI_BL: put2(s, bl_idx + 1); return;
    case MI_LIGHT: src = light_name[light_idx]; break;
    case MI_SCAN: case MI_SPECTRUM: case MI_FDET: src = "Start"; break;
    }
    while ((*s++ = *src++)) { }
}

static int clampi(int v, int lo, int hi) { return v < lo ? lo : v > hi ? hi : v; }

static void menu_change(int item, int dir)
{
    int i;
    switch (item) {
    case MI_BAND: {
        int b = rxband_of(freq10);
        if (b < 0) b = band_last;
        b = (b + dir + NRXBANDS) % NRXBANDS;
        if (!band_freq[b]) band_freq[b] = rxbands[b].start;
        mode = rxbands[b].mode;
        step_idx = rxbands[b].step;
        ofs_idx = rxbands[b].ofs;
        shift = 0;
        audio_close();
        radio_tune(band_freq[b]);
        radio_mode();
        band_last = b;
        break;
    }
    case MI_POWER: power = (power + dir + 4) % 4; break;
    case MI_SHIFT: { int i = shift == 0 ? 0 : shift > 0 ? 1 : 2; i = (i + dir + 3) % 3; shift = i == 0 ? 0 : i == 1 ? 1 : -1; break; }
    case MI_OFFSET: ofs_idx = clampi(ofs_idx + dir, 0, NOFS - 1); break;
    case MI_MIC: mic_gain = clampi(mic_gain + dir, 0, 31); break;
    case MI_TXTONE: i = (tone_index(tone_type, tone_val) + dir + NTONES) % NTONES; tone_from_index(i, &tone_type, &tone_val); break;
    case MI_RXTONE: i = (tone_index(rx_tone_type, rx_tone_val) + dir + NTONES) % NTONES;
                    tone_from_index(i, &rx_tone_type, &rx_tone_val); if (!tx_on) rx_tone(); break;
    case MI_SQ: sq_level = clampi(sq_level + dir, 0, 9); break;
    case MI_STEP: step_idx = clampi(step_idx + dir, 0, (int)(sizeof step_list / sizeof step_list[0]) - 1); break;
    case MI_MODE: mode = (mode + dir + NMODES) % NMODES; audio_close(); radio_mode(); break;
    case MI_TOT: tot_idx = clampi(tot_idx + dir, 0, (int)(sizeof tot_list / sizeof tot_list[0]) - 1); break;
    case MI_BL: bl_idx = clampi(bl_idx + dir, 0, 4); backlight(bl_list[bl_idx]); break;
    case MI_LIGHT: light_idx = clampi(light_idx + dir, 0, (int)sizeof light_list - 1); break;
    case MI_SCAN: case MI_SPECTRUM: case MI_FDET: break;
    }
}

/* # in a tone item: jump to the start of the next tone type (off, CTCSS, DCS normal, DCS inverted). */
static void menu_jump(int item)
{
    if (item != MI_TXTONE && item != MI_RXTONE) return;
    int *ty = item == MI_TXTONE ? &tone_type : &rx_tone_type;
    uint16_t *va = item == MI_TXTONE ? &tone_val : &rx_tone_val;
    static const int start[4] = { 0, 1, 51, 156 };
    tone_from_index(start[(*ty + 1) % 4], ty, va);
    if (item == MI_RXTONE && !tx_on) rx_tone();
}

/* Value before an edit, restored by the red bar. */
static struct { int power, mic, tt, rt, sq, step, mode, tot, bl, shift, ofs, light; uint16_t tv, rv; uint32_t freq; } menu_old;
static void menu_snapshot(void)
{
    menu_old.freq = freq10;
    menu_old.shift = shift; menu_old.ofs = ofs_idx; menu_old.light = light_idx;
    menu_old.power = power; menu_old.mic = mic_gain; menu_old.tt = tone_type; menu_old.tv = tone_val;
    menu_old.rt = rx_tone_type; menu_old.rv = rx_tone_val; menu_old.sq = sq_level; menu_old.step = step_idx;
    menu_old.mode = mode; menu_old.tot = tot_idx; menu_old.bl = bl_idx;
}
static void menu_restore(void)
{
    if (freq10 != menu_old.freq) radio_tune(menu_old.freq);
    shift = menu_old.shift; ofs_idx = menu_old.ofs; light_idx = menu_old.light;
    power = menu_old.power; mic_gain = menu_old.mic; tone_type = menu_old.tt; tone_val = menu_old.tv;
    rx_tone_type = menu_old.rt; rx_tone_val = menu_old.rv; sq_level = menu_old.sq; step_idx = menu_old.step;
    tot_idx = menu_old.tot;
    if (mode != menu_old.mode) { mode = menu_old.mode; audio_close(); radio_mode(); }
    if (bl_idx != menu_old.bl) { bl_idx = menu_old.bl; backlight(bl_list[bl_idx]); }
    if (!tx_on) rx_tone();
}

#define MENU_ROWS  5
#define MENU_Y0    76
#define MENU_PITCH 44
#define MENU_ROW_H 40
static int menu_top;

/* One row: white on black; selected = black on white; being changed = black on green. */
static void menu_row(int item)
{
    int slot = item - menu_top;
    if (slot < 0 || slot >= MENU_ROWS) return;
    int y = MENU_Y0 + slot * MENU_PITCH, sel = item == menu_sel;
    uint16_t bg = sel ? (menu_edit ? C_EDIT : C_WHITE) : C_BLACK, fg = sel ? C_BLACK : C_WHITE;
    if (sel) {
        lcd_fill(0, y, 6, MENU_ROW_H, C_BLACK);
        lcd_fill(LCD_W - 6, y, 6, MENU_ROW_H, C_BLACK);
        lcd_fill(6, y, 4, 4, C_BLACK);
        lcd_fill(LCD_W - 10, y, 4, 4, C_BLACK);
        lcd_fill(6, y + MENU_ROW_H - 4, 4, 4, C_BLACK);
        lcd_fill(LCD_W - 10, y + MENU_ROW_H - 4, 4, 4, C_BLACK);
        fill_rrect(6, y, LCD_W - 12, MENU_ROW_H, bg);
    } else {
        lcd_fill(0, y, LCD_W, MENU_ROW_H, C_BLACK);
    }
    int ty = y + (MENU_ROW_H - F_MED->height) / 2;
    char nm[16] = { menu_key_of[item], menu_key_of[item] == ' ' ? ' ' : '.', ' ' };
    for (int k = 0; menu_name[item][k]; k++) { nm[3 + k] = menu_name[item][k]; nm[4 + k] = 0; }
    aa_text(16, ty, F_MED, nm, fg, bg);
    char v[16];
    menu_value(item, v);
    aa_text(LCD_W - 18 - aa_width(F_MED, v), ty, F_MED, v, fg, bg);
}

static void menu_rows(void)
{
    for (int k = 0; k < MENU_ROWS; k++) menu_row(menu_top + k);
}

/* Move the selection; scroll only when it leaves the visible rows. */
static void menu_select(int item)
{
    int old = menu_sel;
    menu_sel = item;
    int top = menu_top;
    if (item < top) top = item;
    if (item >= top + MENU_ROWS) top = item - MENU_ROWS + 1;
    if (top != menu_top) { menu_top = top; menu_rows(); }
    else { menu_row(old); menu_row(item); }
}

static void menu_show(void)
{
    menu_open = 1;
    menu_edit = 0;
    clear_band(34, MENU_Y0);
    aa_text_c(42, F_MED, "MENU", C_WHITE, C_BLACK);
    separator(70);
    for (int k = 0; k < MENU_ROWS; k++) clear_band(MENU_Y0 + k * MENU_PITCH + MENU_ROW_H, MENU_Y0 + (k + 1) * MENU_PITCH);
    clear_band(MENU_Y0 + MENU_ROWS * MENU_PITCH, Y_SOFT);
    menu_rows();
    soft_keys("OK", "EXIT");
}

static void menu_close(void)
{
    menu_open = 0;
    menu_edit = 0;
    draw_home();
}

static void menu_key(char k)
{
    if (!menu_edit) {
        if (k == K_UP) menu_select((menu_sel + MI_COUNT - 1) % MI_COUNT);
        else if (k == K_DOWN) menu_select((menu_sel + 1) % MI_COUNT);
        else if (k == K_MENU && menu_sel == MI_SCAN) { menu_close(); scan_start(); }
        else if (k == K_MENU && menu_sel == MI_SPECTRUM) { menu_open = 0; menu_edit = 0; draw_home(); spectrum_start(); }
        else if (k == K_MENU && menu_sel == MI_FDET) { menu_close(); fdet_start(); }
        else if (k == K_MENU) { menu_snapshot(); menu_edit = 1; menu_row(menu_sel); }
        else if (k == K_EXIT) menu_close();
        else for (int i = 0; i < MI_COUNT; i++) if (menu_key_of[i] == k) { menu_snapshot(); menu_edit = 1; menu_select(i); break; }
        return;
    }
    if (k == K_UP || k == K_DOWN) menu_change(menu_sel, k == K_UP ? 1 : -1);
    else if (k == '#') menu_jump(menu_sel);
    else if (k == K_MENU) menu_edit = 0;
    else if (k == K_EXIT) { menu_restore(); menu_edit = 0; menu_rows(); return; }
    else return;
    if (menu_sel == MI_BAND) menu_rows(); else menu_row(menu_sel);
}

/* ---------------- commands ---------------- */
static int hex_val(const char *s, uint32_t *out)
{
    uint32_t v = 0;
    int n = 0;
    while (*s == ' ') s++;
    for (; *s; s++, n++) {
        char c = *s;
        if (c >= '0' && c <= '9') v = v * 16 + (uint32_t)(c - '0');
        else if (c >= 'A' && c <= 'F') v = v * 16 + (uint32_t)(c - 'A' + 10);
        else break;
    }
    *out = v;
    return n;
}

/* "446.00625" (MHz) -> 10 Hz units. */
static int parse_mhz(const char *s, uint32_t *out)
{
    uint32_t whole = 0, frac = 0;
    int nd = 0, nf = 0;
    while (*s == ' ') s++;
    for (; *s >= '0' && *s <= '9'; s++, nd++) whole = whole * 10 + (uint32_t)(*s - '0');
    if (*s == '.') {
        s++;
        for (; *s >= '0' && *s <= '9'; s++) if (nf < 5) { frac = frac * 10 + (uint32_t)(*s - '0'); nf++; }
    }
    if (!nd || whole > 1300) return 0;
    while (nf < 5) { frac *= 10; nf++; }
    *out = whole * 100000 + frac;
    return 1;
}

/* Signed decimal; returns 0 if no digits. */
static int parse_int(const char *s, int *out)
{
    int neg = 0, v = 0, n = 0;
    while (*s == ' ') s++;
    if (*s == '-') { neg = 1; s++; }
    for (; *s >= '0' && *s <= '9'; s++, n++) v = v * 10 + (*s - '0');
    *out = neg ? -v : v;
    return n;
}

static void report_freq(void)
{
    char s[16];
    fmt_freq(s, freq10);
    uart_puts("FREQ "); uart_puts(s); uart_puts(" MHZ "); uart_puts(mode_name[mode]); uart_puts("\r\n");
}

/* "OFF", "88.5" (CTCSS Hz, 60.0-260.0), "D023N" / "D023I" (DCS). Returns 0 if not valid. */
static int parse_tone(const char *a, int *type, uint16_t *val)
{
    if (!__builtin_strcmp(a, "OFF")) { *type = TONE_OFF; *val = 0; return 1; }
    if (a[0] == 'D') {
        if (a[1] < '0' || a[1] > '7' || a[2] < '0' || a[2] > '7' || a[3] < '0' || a[3] > '7' ||
            (a[4] != 'N' && a[4] != 'I') || a[5] != 0) return 0;
        *type = a[4] == 'I' ? TONE_DCS_I : TONE_DCS_N;
        *val = (uint16_t)(((a[1] - '0') << 6) | ((a[2] - '0') << 3) | (a[3] - '0'));
        return 1;
    }
    uint32_t v = 0;
    int digits = 0, dec = -1;
    for (; *a; a++) {
        if (*a == '.' && dec < 0) { dec = 0; continue; }
        if (*a < '0' || *a > '9' || dec >= 1 || ++digits > 4) return 0;
        v = v * 10 + (uint32_t)(*a - '0');
        if (dec >= 0) dec++;
    }
    if (dec < 1) v *= 10;
    if (v < 600 || v > 2600) return 0;
    *type = TONE_CTCSS;
    *val = (uint16_t)v;
    return 1;
}

static void command(char *cmd)
{
    for (char *p = cmd; *p; p++) if (*p >= 'a' && *p <= 'z') *p = (char)(*p - 32);

    if (cmd[0] == 0) {
        return;
    } else if (!__builtin_strcmp(cmd, "PING")) {
        uart_puts("PONG\r\n");
    } else if (!__builtin_strcmp(cmd, "VER")) {
        uart_puts(FW_NAME " " __DATE__ " " __TIME__ "\r\n");
    } else if (cmd[0] == 'F' && (cmd[1] == ' ' || cmd[1] == 0)) {
        uint32_t f;
        if (cmd[1] == 0) { report_freq(); return; }
        if (tx_on) { uart_puts("ERR transmitting\r\n"); return; }
        if (!parse_mhz(cmd + 1, &f) || !radio_tune(f)) { uart_puts("ERR frequency (18-660, 840-1300 MHz)\r\n"); return; }
        show_freq();
        report_freq();
    } else if (cmd[0] == 'M' && cmd[1] == ' ') {
        const char *m = cmd + 2;
        int n = -1;
        for (int i = 0; i < NMODES; i++) if (!__builtin_strcmp(m, mode_name[i])) n = i;
        if (n < 0) { uart_puts("ERR mode FM, NFM, AM or USB\r\n"); return; }
        if (tx_on) { uart_puts("ERR transmitting\r\n"); return; }
        mode = n;
        audio_close();
        radio_mode();
        show_freq();
        report_freq();
    } else if (cmd[0] == 'S' && cmd[1] == 'Q' && cmd[2] == ' ') {
        int v;
        if (!parse_int(cmd + 3, &v) || v < 0 || v > 9) { uart_puts("ERR SQ 0-9\r\n"); return; }
        sq_level = v;
        show_info();
        uart_puts("SQ "); put_dec(sq_level); uart_puts("\r\n");
    } else if (cmd[0] == 'V' && cmd[1] == 'O' && cmd[2] == 'L' && cmd[3] == ' ') {
        int v;
        if (!parse_int(cmd + 4, &v) || v < 0 || v > 63) { uart_puts("ERR VOL 0-63\r\n"); return; }
        volume = v;
        if (audio_on) bk1_write(0x48, rx_gain());
        uart_puts("VOL "); put_dec(volume); uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "ADC")) {
        uart_puts("PC2 "); put_dec(adc_read(ADC_CHANNEL_12));
        uart_puts(" PA7 "); put_dec(adc_read(ADC_CHANNEL_7)); uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "RSSI")) {
        uart_puts("RSSI "); put_dec(rssi_dbm()); uart_puts(" DBM\r\n");
    } else if ((cmd[0] == 'R') && (cmd[1] == '1' || cmd[1] == '2')) {
        uint32_t reg;
        if (!hex_val(cmd + 2, &reg) || reg > 0x7F) { uart_puts("ERR register 00-7F\r\n"); return; }
        int chip = cmd[1] - '1';
        uint16_t v = bk_read(chip, (uint8_t)reg);
        uart_puts("R"); uart_putc((char)('1' + chip)); uart_puts(" ");
        put_hex(reg, 2); uart_puts(" = "); put_hex(v, 4); uart_puts("\r\n");
    } else if (cmd[0] == 'T' && (cmd[1] == ' ' || cmd[1] == 0)) {
        if (cmd[1] == ' ') {
            if (!parse_tone(cmd + 2, &tone_type, &tone_val)) {
                uart_puts("ERR T OFF, T 88.5 (CTCSS) or T D023N / T D023I (DCS)\r\n"); return;
            }
            if (tx_on) tx_tone();
            show_chips();
        }
        char s[12];
        uart_puts("T "); uart_puts(tone_text(s) ? s : "OFF"); uart_puts("\r\n");
    } else if (cmd[0] == 'R' && cmd[1] == 'T' && (cmd[2] == ' ' || cmd[2] == 0)) {
        if (cmd[2] == ' ') {
            if (!parse_tone(cmd + 3, &rx_tone_type, &rx_tone_val)) {
                uart_puts("ERR RT OFF, RT 88.5 (CTCSS) or RT D023N / RT D023I (DCS)\r\n"); return;
            }
            if (!tx_on) rx_tone();
            show_chips();
        }
        char s[12];
        uart_puts("RT "); uart_puts(tone_text_of(rx_tone_type, rx_tone_val, s) ? s : "OFF"); uart_puts("\r\n");
    } else if (cmd[0] == 'P' && cmd[1] == 'O' && cmd[2] == 'W' && (cmd[3] == ' ' || cmd[3] == 0)) {
        if (cmd[3] == ' ') {
            int n = -1;
            for (int i = 0; i < 4; i++) if (!__builtin_strcmp(cmd + 4, power_name[i])) n = i;
            if (n < 0) { uart_puts("ERR POW MIN, LOW, MID or HIGH\r\n"); return; }
            if (tx_on) { uart_puts("ERR transmitting\r\n"); return; }
            power = n;
            show_chips();
        }
        uart_puts("POW "); uart_puts(power_name[power]); uart_puts("\r\n");
    } else if (cmd[0] == 'F' && cmd[1] == 'O' && cmd[2] == 'N' && cmd[3] == 'T' && cmd[4] == ' ' &&
               (cmd[5] == '1' || cmd[5] == '2') && !cmd[6]) {
        F_MED = cmd[5] == '1' ? &font_med : &font_med2;
        F_SMALL = cmd[5] == '1' ? &font_small : &font_small2;
        draw_home();
        uart_puts("FONT "); uart_putc(cmd[5]); uart_puts("\r\n");
    } else if (cmd[0] == 'E' && cmd[1] == 'D' && cmd[2] == 'G' && cmd[3] == 'E' && cmd[4] == ' ' &&
               cmd[5] >= '0' && cmd[5] <= '3' && !cmd[6]) {
        edge = cmd[5] - '0';
        draw_home();
        uart_puts("EDGE "); uart_putc(cmd[5]); uart_puts("\r\n");
    } else if (cmd[0] == 'P' && cmd[1] == 'A' && cmd[2] == 'L' && cmd[3] == ' ' &&
               (cmd[4] == '1' || cmd[4] == '2') && !cmd[5]) {
        P = &palettes[cmd[4] - '1'];
        draw_home();
        uart_puts("PAL "); uart_putc(cmd[4]); uart_puts("\r\n");
    } else if (!__builtin_strncmp(cmd, "SHIFT", 5) && (cmd[5] == ' ' || cmd[5] == 0)) {
        if (cmd[5] == ' ') {
            const char *a = cmd + 6;
            if (!__builtin_strcmp(a, "OFF")) shift = 0;
            else if (!__builtin_strcmp(a, "+")) shift = 1;
            else if (!__builtin_strcmp(a, "-")) shift = -1;
            else { uart_puts("ERR SHIFT OFF, + or -\r\n"); return; }
            show_chips();
        }
        uart_puts("SHIFT "); uart_puts(shift > 0 ? "+" : shift < 0 ? "-" : "OFF"); uart_puts("\r\n");
    } else if (!__builtin_strncmp(cmd, "OFFSET", 6) && (cmd[6] == ' ' || cmd[6] == 0)) {
        if (cmd[6] == ' ') {
            uint32_t f;
            int n = -1;
            if (parse_mhz(cmd + 7, &f)) for (int i = 0; i < NOFS; i++) if (ofs_list[i] == f) n = i;
            if (n < 0) { uart_puts("ERR OFFSET 0.1, 0.5, 0.6, 1, 1.6, 5, 6, 7.6 or 12 (MHz)\r\n"); return; }
            ofs_idx = n;
            show_chips();
        }
        uart_puts("OFFSET "); uart_puts(ofs_name[ofs_idx]); uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "PTT ON")) {
        if (!tx_allowed(tx_freq())) { uart_puts("ERR transmit not allowed on this frequency\r\n"); return; }
        serial_ptt = 1;
        serial_ptt_until = ticks + SERIAL_PTT_MS;
        uart_puts("PTT ON\r\n");
    } else if (!__builtin_strcmp(cmd, "PTT OFF")) {
        serial_ptt = 0;
        uart_puts("PTT OFF\r\n");
    } else if (!__builtin_strcmp(cmd, "PTT")) {
        uart_puts(tx_on ? "PTT ON\r\n" : "PTT OFF\r\n");
    } else if (cmd[0] == 'V' && cmd[1] == 'F' && cmd[2] == 'O' && (cmd[3] == ' ' || cmd[3] == 0)) {
        if (cmd[3] == ' ') {
            if ((cmd[4] != 'A' && cmd[4] != 'B') || cmd[5]) { uart_puts("ERR VFO A or B\r\n"); return; }
            if (tx_on) { uart_puts("ERR transmitting\r\n"); return; }
            if (cmd[4] - 'A' != vfo_cur) { vfo_select(cmd[4] - 'A'); show_freq(); show_status("RX", C_GREEN); }
        }
        uart_puts(vfo_cur ? "VFO B\r\n" : "VFO A\r\n");
        report_freq();
    } else if (!__builtin_strcmp(cmd, "SPIID")) {
        uart_puts("SPIID "); put_hex(flash_id(), 6); uart_puts("\r\n");
    } else if (!__builtin_strncmp(cmd, "SPIRD ", 6)) {
        uint32_t a, n = 16;
        char *q = cmd + 6;
        if (!hex_val(q, &a)) { uart_puts("ERR SPIRD addr [len] (hex)\r\n"); return; }
        while (*q && *q != ' ') q++;
        if (*q == ' ' && !hex_val(q + 1, &n)) { uart_puts("ERR SPIRD addr [len] (hex)\r\n"); return; }
        if (n > 64) n = 64;
        uint8_t b[64];
        flash_read(a, b, (int)n);
        uart_puts("SPIRD "); put_hex(a, 6); uart_puts(":");
        for (uint32_t i = 0; i < n; i++) { uart_puts(" "); put_hex(b[i], 2); }
        uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "CAL")) {
        uart_puts(cal_ok ? "CAL FROM SPI FLASH, ID " : "CAL BUILT-IN, ID "); put_hex(flash_jedec, 6);
        uart_puts(", HEADER");
        for (int i = 0; i < 12; i++) { uart_puts(" "); put_hex(cal_hdr[i], 2); }
        uart_puts("\r\n");
    } else if (!__builtin_strncmp(cmd, "AGC", 3) && (cmd[3] == ' ' || cmd[3] == 0)) {
        if (!__builtin_strcmp(cmd, "AGC ON")) agc_on = 1;
        else if (!__builtin_strcmp(cmd, "AGC OFF")) agc_on = 0;
        else if (cmd[3]) { uart_puts("ERR AGC ON or OFF\r\n"); return; }
        uart_puts(agc_on ? "AGC ON, STEP " : "AGC OFF, STEP "); put_dec(agc_idx); uart_puts("\r\n");
    } else if (cmd[0] == 'W' && cmd[1] == '1' && cmd[2] == ' ') {
        /* Experiments only. Transmit-related registers are refused. */
        uint32_t reg, val;
        char *q = cmd + 3;
        if (!hex_val(q, &reg) || reg > 0x7F) { uart_puts("ERR W1 reg value (hex)\r\n"); return; }
        while (*q && *q != ' ') q++;
        if (*q != ' ' || !hex_val(q + 1, &val) || val > 0xFFFF) { uart_puts("ERR W1 reg value (hex)\r\n"); return; }
        if (reg == 0x30 || reg == 0x33 || reg == 0x36 || reg == 0x37 || reg == 0x52) { uart_puts("ERR register not allowed\r\n"); return; }
        bk1_write((uint8_t)reg, (uint16_t)val);
        uart_puts("W1 "); put_hex(reg, 2); uart_puts(" = "); put_hex(val, 4); uart_puts("\r\n");
    } else if (cmd[0] == 'M' && cmd[1] == 'I' && cmd[2] == 'C' && (cmd[3] == ' ' || cmd[3] == 0)) {
        int v;
        if (cmd[3] == ' ') {
            if (!parse_int(cmd + 4, &v) || v < 0 || v > 31) { uart_puts("ERR MIC 0-31\r\n"); return; }
            mic_gain = v;
            if (tx_on) bk1_write(0x7D, (uint16_t)(0xE940 + mic_gain));
            show_chips();
        }
        uart_puts("MIC "); put_dec(mic_gain); uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "CLK")) {
        uart_puts("CLK "); put_dec((int32_t)(system_core_clock / 1000000u));
        uart_puts(" MHZ, BOOTLOADER LEFT "); put_dec((int32_t)(boot_clock / 1000000u)); uart_puts(" MHZ\r\n");
    } else if (!__builtin_strcmp(cmd, "HELP")) {
        uart_puts("PING, VER, F [MHz], M FM|NFM|AM|USB, RSSI, SQ 0-9, VOL 0-63, MIC [0-31], POW [MIN|LOW|MID|HIGH], T [OFF|67.0|D023N|D023I], RT [same, receive], SHIFT [OFF|+|-], OFFSET [MHz], VFO [A|B], PTT ON|OFF (repeat ON within 2 s), ADC, R1 xx, R2 xx. TX: PTT1 voice, alarm button 1750 Hz, amateur bands only.\r\n");
    } else {
        uart_puts("ERR unknown command, try HELP\r\n");
    }
}

/* ---------------- watchdog ---------------- */
static void watchdog_start(void)
{
    wdt_register_write_enable(TRUE);
    wdt_divider_set(WDT_CLK_DIV_32);
    wdt_reload_value_set(2500);          /* about 2 s with the ~40 kHz internal clock */
    wdt_counter_reload();
    wdt_enable();
}

/* ---------------- main ---------------- */
int main(void)
{
    bootloader_cleanup();
    system_core_clock_update();
    SysTick_Config(system_core_clock / 1000);

    crm_periph_clock_enable(CRM_GPIOA_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOB_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOC_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOD_PERIPH_CLOCK, TRUE);
    crm_periph_clock_enable(CRM_GPIOF_PERIPH_CLOCK, TRUE);

    /* Transmit path off first: amplifier pins low, then BK4819 #1 reset and off. */
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    pin_out(GPIOC, GPIO_PINS_15);
    pin_out(GPIOB, GPIO_PINS_11);
    bk_pins();
    bk1_write(0x00, 0x8000);
    tx_kill();
    clock_config();
    SysTick_Config(system_core_clock / 1000);
    pvm_init();
    gpio_bits_set(GPIOF, GPIO_PINS_0);
    pin_out(GPIOF, GPIO_PINS_0);

    /* Remaining start-up pin states of the stock board setup (0x8011AC8). */
    gpio_bits_reset(GPIOA, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_9);
    pin_out(GPIOA, GPIO_PINS_0 | GPIO_PINS_1 | GPIO_PINS_9);
    gpio_bits_reset(GPIOB, GPIO_PINS_9);
    gpio_bits_set(GPIOB, GPIO_PINS_7);
    pin_out(GPIOB, GPIO_PINS_7 | GPIO_PINS_9);
    gpio_bits_reset(GPIOC, GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5);
    pin_out(GPIOC, GPIO_PINS_3 | GPIO_PINS_4 | GPIO_PINS_5);
    gpio_bits_reset(GPIOF, GPIO_PINS_1);
    pin_out(GPIOF, GPIO_PINS_1);

    gpio_bits_reset(GPIOA, GPIO_PINS_13 | GPIO_PINS_14);
    pin_out(GPIOA, GPIO_PINS_13 | GPIO_PINS_14);

    uart_init();
    uart_puts("\r\n" FW_NAME " ready. Type HELP.\r\n");

    gpio_bits_reset(GPIOF, GPIO_PINS_8);
    pin_out(GPIOF, GPIO_PINS_8);                  /* speaker amplifier off */

    keys_init();
    adc_setup();
    lcd_init();
    lcd_fill(0, 0, LCD_W, LCD_H, C_BLACK);
    backlight(bl_list[bl_idx]);
    gpio_bits_set(GPIOA, GPIO_PINS_9);              /* keypad light */
    splash();
    top_bar();

    flash_init();
    cal_load();
    radio_init();
    show_freq();
    show_status("RX", C_GREEN);
    show_info();
    show_batt();
    meter_scale();
    report_freq();

    watchdog_start();

    char buf[32];
    int len = 0;
    char entry[8] = "";
    int elen = 0;
    uint16_t last_m = 0;
    int last_key = 0, tx_lockout = 0, alarm_count = 0;
    uint32_t rep_at = 0, green_since = 0;
    int green_used = 1;
    uint32_t tx_until = 0;
    uint32_t next_scan = 0, next_rssi = 0, next_sq = 0, next_batt = 10000;
    int last_rssi = -160, last_noise = 127, open_count = 0, close_count = 0, last_side = SIDE_NONE;

    for (;;) {
        wdt_counter_reload();

        int c = uart_getc();
        if (c >= 0) {
            if (c == '\r' || c == '\n') {
                buf[len] = 0;
                command(buf);
                len = 0;
            } else if (len < (int)sizeof buf - 1) {
                buf[len++] = (char)c;
            }
        }

        if ((int32_t)(ticks - next_scan) >= 0) {
            next_scan = ticks + 20;

            /* PTT1 = voice. Alarm button = 1750 Hz burst, alone or while PTT1 is held. */
            int ptt = ptt_down();
            alarm_count = side_key() == SIDE_ALARM ? (alarm_count < 2 ? alarm_count + 1 : 2) : 0;
            int alarm = alarm_count >= 2;
            if (serial_ptt && (int32_t)(ticks - serial_ptt_until) >= 0) {
                serial_ptt = 0;
                uart_puts("PTT TIMEOUT\r\n");
            }
            int key = ptt || alarm || serial_ptt;
            if (tx_on && (!key || (int32_t)(ticks - tx_until) >= 0)) {
                if (key) tx_lockout = 1;
                tx_stop();
                show_status(tx_lockout ? "RX (TIME-OUT)" : "RX", tx_lockout ? C_YELLOW : C_GREEN);
                uart_puts(tx_lockout ? "TX OFF TOT\r\n" : "TX OFF\r\n");
            } else if (tx_on && alarm != tx_burst) {
                tx_audio(alarm);
                show_tx();
                uart_puts(alarm ? "TX 1750\r\n" : "TX VOICE\r\n");
            } else if (key && !last_key && !tx_on && !tx_lockout) {
                wake();
                scan_stop();
                if (spectrum_on) spectrum_stop();
                if (fdet_on) fdet_stop();
                if (menu_open) menu_close();
                if (tx_start(alarm)) {
                    tx_until = ticks + (uint32_t)tot_list[tot_idx] * 1000u;
                    show_tx();
                    uart_puts(alarm ? "TX ON 1750\r\n" : "TX ON\r\n");
                } else {
                    show_status("TX NOT ALLOWED", C_YELLOW);
                    uart_puts("TX REFUSED outside the transmit bands\r\n");
                }
            }
            if (!key) tx_lockout = 0;
            last_key = key;

            uint16_t m = keys_matrix();
            uint16_t prev_m = last_m;
            uint16_t pressed = m & (uint16_t)~last_m;
            if (pressed) {
                int was_dark = !lit;
                wake();
                if (scanning) { scan_stop(); pressed = 0; }
                if (spectrum_on) { spectrum_stop(); pressed = 0; }
                if (fdet_on) { fdet_stop(); show_status("RX", C_GREEN); pressed = 0; }
                if (was_dark) pressed = 0;                     /* the first press only wakes the screen */
            }
            const uint16_t rep_keys = (1u << 1) | (1u << 2);   /* up, down repeat when held */
            if (m & rep_keys) {
                if (!(last_m & m & rep_keys)) rep_at = ticks + 450;
                else if ((int32_t)(ticks - rep_at) >= 0) { pressed |= m & rep_keys; rep_at = ticks + 90; }
            }
            last_m = m;
            if (!menu_open) {
                /* Green bar on the home screen: held 1.5 s = key lock on/off, short = menu on release. */
                const uint16_t green = 1u << 0;
                if ((m & green) && !(prev_m & green)) { green_since = ticks; green_used = 0; }
                if ((m & green) && !green_used && (int32_t)(ticks - green_since) >= 1500) {
                    green_used = 1;
                    locked = !locked;
                    show_status(locked ? "KEYS LOCKED" : "KEYS UNLOCKED", C_YELLOW);
                }
                if (!(m & green) && (prev_m & green) && !green_used && !locked && lit) menu_show();
                pressed &= (uint16_t)~green;
                if (locked && pressed) { show_status("KEYS LOCKED", C_YELLOW); pressed = 0; }
            }
            for (int b = 0; b < 16 && !tx_on; b++) {
                if (!(pressed & (1u << b))) continue;
                char k = key_char[b];
                int changed = 0;
                if (menu_open) { menu_key(k); continue; }
                if (k >= '0' && k <= '9') {
                    if (elen < 7) { entry[elen++] = k; entry[elen] = 0; }
                    show_entry(entry);
                } else if (k == K_EXIT) {
                    elen = 0; entry[0] = 0;
                    show_entry(entry);
                } else if (k == '#' && !elen) {
                    if (shift) {
                        uint32_t f = tx_freq();
                        shift = -shift;
                        changed = radio_tune(f);
                    }
                } else if (k == '#') {
                    uint32_t khz = 0;
                    for (int i = 0; i < elen; i++) khz = khz * 10 + (uint32_t)(entry[i] - '0');
                    elen = 0; entry[0] = 0;
                    show_entry(entry);
                    changed = radio_tune(khz * 100);
                    if (!changed) show_status("BAD FREQUENCY", C_YELLOW);
                } else if (k == '*') {
                    vfo_select(vfo_cur ^ 1);
                    show_info();
                    changed = 1;
                } else if (k == K_UP || k == K_DOWN) {
                    uint32_t f = k == K_UP ? freq10 + step_list[step_idx] : freq10 - step_list[step_idx];
                    changed = radio_tune(f);
                }
                if (changed) {
                    show_freq();
                    show_status("RX", C_GREEN);
                    report_freq();
                }
            }
        }

        /* Squelch every 50 ms; side keys force it open. */
        if (!tx_on && !spectrum_on && !fdet_on && (int32_t)(ticks - next_sq) >= 0) {
            next_sq = ticks + 50;
            last_rssi = rssi_dbm();
            last_noise = bk_read(0, 0x65) & 0x7F;
            agc_run(last_rssi);
            int side = side_key();
            if (side != last_side) {
                if (last_side != SIDE_NONE) { uart_puts(side_name[last_side]); uart_puts(" up\r\n"); }
                if (side != SIDE_NONE) { uart_puts(side_name[side]); uart_puts(" down\r\n"); }
                last_side = side;
            }
            int monitor = side == SIDE_KEY1 || side == SIDE_KEY2;
            int open_noise = 70 - 5 * sq_level;
            int open_rssi = -80 + 2 * sq_level;
            int carrier = mode == MODE_AM || mode == MODE_USB
                ? last_rssi >= (audio_on ? open_rssi - 3 : open_rssi)
                : last_noise <= (audio_on ? open_noise + 10 : open_noise);
            int tone_ok = 1;
            if (rx_tone_type != TONE_OFF && (mode == MODE_FM || mode == MODE_NFM)) {
                /* Tone match flags in 0x0C (stock 0x8004444): bit 10 CTCSS, 14 DCS normal, 15 DCS inverted. */
                uint16_t r0c = bk_read(0, 0x0C);
                tone_ok = rx_tone_type == TONE_CTCSS ? (r0c >> 10) & 1
                        : rx_tone_type == TONE_DCS_N ? (r0c >> 14) & 1
                        : (r0c >> 15) & 1;
            }
            int want = sq_level == 0 || monitor || (carrier && tone_ok);
            if (want && !audio_on) {
                close_count = 0;
                if (++open_count >= 2) {
                    open_count = 0;
                    audio_open();
                    wake();
                    show_status(monitor ? "RX MONITOR" : "RX OPEN", C_GREEN);
                }
            } else if (!want && audio_on) {
                open_count = 0;
                if (++close_count >= 4) {
                    close_count = 0;
                    audio_close();
                    show_status("RX", C_GREEN);
                }
            } else {
                open_count = close_count = 0;
            }
            /* LEDs: green = audio open (audio_open/close), yellow = carrier held back by the tone
             * squelch, red = transmit (tx_start/tx_stop), off otherwise. */
            if (!audio_on && carrier && !tone_ok) gpio_bits_set(GPIOA, GPIO_PINS_13 | GPIO_PINS_14);
            else {
                gpio_bits_reset(GPIOA, GPIO_PINS_14);
                if (!audio_on) gpio_bits_reset(GPIOA, GPIO_PINS_13);
            }
        }

        if (!tx_on && !spectrum_on && (int32_t)(ticks - next_rssi) >= 0) {
            next_rssi = ticks + 300;
            show_rssi(last_rssi, last_noise);
        }

        if (scanning && !tx_on && !menu_open) scan_step();
        if (spectrum_on && !tx_on) spectrum_sweep();
        if (fdet_on && !tx_on) fdet_poll();

        if (lit && light_list[light_idx] && !tx_on && !audio_on && !menu_open &&
            (int32_t)(ticks - last_activity) >= (int32_t)light_list[light_idx] * 1000) {
            lit = 0;
            backlight(0);
            gpio_bits_reset(GPIOA, GPIO_PINS_9);
        }

        if (!tx_on && (int32_t)(ticks - next_batt) >= 0) {
            next_batt = ticks + 10000;
            show_batt();
            if (batt_v <= 68 && !audio_on) show_status("LOW BATTERY", C_YELLOW);
        }

    }
}
