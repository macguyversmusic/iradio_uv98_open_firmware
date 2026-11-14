/* UV-98 Plus / RT-880G open firmware, test 10: receive FM/NFM/AM/USB, squelch, side keys, voice TX on
 * the amateur bands (power MIN by default, LOW/MID/HIGH from calibration), CTCSS/DCS on TX, 1750 Hz
 * burst on the alarm button, 60 s time-out timer, CTCSS/DCS tone squelch on receive (test 11).
 * Pin map and stock sequences: ../FINDINGS.md. BK4819 #1 is the radio; #2 is reset and put to sleep. */
#include "at32f423.h"
#include "font.h"

#define FW_NAME  "UV98+ open test 11"
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

/* Glyph at (x, y), scaled 1x or 2x. */
static void lcd_char(int x, int y, char ch, int scale, uint16_t fg, uint16_t bg)
{
    if (ch < FONT_FIRST || ch > FONT_LAST) ch = '?';
    const uint16_t *g = font_glyphs[ch - FONT_FIRST];
    int w = FONT_W * scale, h = FONT_H * scale;
    lcd_window(x, y, x + w - 1, y + h - 1);
    for (int col = 0; col < w; col++) {
        uint16_t mask = (uint16_t)(0x8000u >> (col / scale));
        for (int row = h - 1; row >= 0; row--) {
            uint16_t c = (g[row / scale] & mask) ? fg : bg;
            spi_send((uint8_t)(c >> 8));
            spi_send((uint8_t)c);
        }
    }
}

/* Text on a 15 x 13 grid of 16 x 24 cells; pads the rest of the line. */
static void lcd_line(int row, const char *s, uint16_t fg, uint16_t bg)
{
    int col = 0;
    for (; *s && col < LCD_W / FONT_W; col++) lcd_char(col * FONT_W, row * FONT_H, *s++, 1, fg, bg);
    for (; col < LCD_W / FONT_W; col++) lcd_char(col * FONT_W, row * FONT_H, ' ', 1, fg, bg);
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

/* Transmit is allowed only inside these ranges (10 Hz units, [lo, hi)): the amateur
 * allocations of all three ITU regions that this chip can tune, plus PMR446. */
static const struct { uint32_t lo, hi; } tx_ranges[] = {
    {   1806800,   1816800 },   /* 17 m   18.068-18.168 */
    {   2100000,   2145000 },   /* 15 m   21.000-21.450 */
    {   2489000,   2499000 },   /* 12 m   24.890-24.990 */
    {   2800000,   2970000 },   /* 10 m   28.000-29.700 */
    {   4066000,   4070000 },   /* 8 m    40.660-40.700 (some Region 1 countries) */
    {   5000000,   5400000 },   /* 6 m    50-54 */
    {   6990000,   7050000 },   /* 4 m    69.9-70.5 (some Region 1 countries) */
    {  14400000,  14800000 },   /* 2 m    144-148 */
    {  21900000,  22500000 },   /* 1.25 m 219-225 (Region 2) */
    {  42000000,  45000000 },   /* 70 cm  420-450 (includes PMR446) */
    {  90200000,  92800000 },   /* 33 cm  902-928 (Region 2) */
    { 124000000, 130000000 },   /* 23 cm  1240-1300 */
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
static uint16_t power_reg(uint32_t f)
{
    if (power == PWR_MIN) return 0;
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
#define TX_MAX_MS    60000    /* time-out timer */

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

static uint32_t freq10 = 44600625;   /* start on PMR446 channel 1, 446.00625 MHz */
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
    uint32_t v = f + b->cal - 0x8000u;
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
    bk1_write(0x54, 0x8F88);
    bk1_write(0x55, 0x3201);
    bk1_write(0x75, 0xFA02);
    bk1_write(0x74, 0xE61C);
    bk1_write(0x44, 0x8F88);
    bk1_write(0x45, 0x3201);
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

static int tx_allowed(uint32_t f)
{
    for (unsigned i = 0; i < sizeof tx_ranges / sizeof tx_ranges[0]; i++)
        if (f >= tx_ranges[i].lo && f < tx_ranges[i].hi) return 1;
    return 0;
}

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
    const band_t *b = band_of(freq10);
    if (!b || !tx_allowed(freq10)) return 0;
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
    tx_tone();
    bk1_write(0x71, 0x68DC);                                     /* scrambler off */
    bk1_write(0x70, 0x0000);                                     /* no tone */
    bk1_write(0x74, 0xE61C);                                     /* TX 3 kHz response */
    bk1_write(0x7D, (uint16_t)(0xE940 + mic_gain));
    bk1_write(0x40, TX_DEVIATION);
    bk1_write(0x37, 0x1D0F);
    delay_ms(10);
    bk1_write(0x52, 0x028F);
    bk1_write(0x30, 0x0200);
    bk1_write(0x30, 0xC1FE);
    tx_burst = 0;
    if (burst) tx_audio(1);
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    uint16_t p = power_reg(freq10);
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

static void show_freq(void)
{
    char s[16];
    fmt_freq(s, freq10);
    char big[8];
    int i = 0;
    for (; s[i] && i < 7; i++) big[i] = s[i];        /* "145.800" / "446.006" */
    big[i] = 0;
    int x = (LCD_W - 7 * FONT_W * 2) / 2;
    for (int k = 0; k < 7; k++) lcd_char(x + k * FONT_W * 2, 36, big[k] ? big[k] : ' ', 2, C_WHITE, C_BLACK);
    char line[16];
    int p = 0;
    for (int k = 0; s[k]; k++) line[p++] = s[k];
    line[p++] = ' ';
    for (const char *m = mode_name[mode]; *m; m++) line[p++] = *m;
    line[p] = 0;
    lcd_line(4, line, C_GREEN, C_BLACK);
}

static void show_rssi(int dbm, int noise)
{
    char s[16] = "RSSI ";
    int p = 5;
    int v = dbm < 0 ? -dbm : dbm;
    if (dbm < 0) s[p++] = '-';
    char t[4]; int n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) s[p++] = t[--n];
    s[p++] = ' '; s[p++] = 'N';
    v = noise; n = 0;
    do { t[n++] = (char)('0' + v % 10); v /= 10; } while (v);
    while (n) s[p++] = t[--n];
    s[p++] = ' '; s[p++] = 'S'; s[p++] = 'Q'; s[p++] = (char)('0' + sq_level);
    s[p] = 0;
    lcd_line(6, s, C_WHITE, C_BLACK);
    int bars = (dbm + 130) / 5;           /* -130 dBm -> 0, -55 dBm -> 15 */
    if (bars < 0) bars = 0;
    if (bars > 15) bars = 15;
    char b[16];
    for (int k = 0; k < 15; k++) b[k] = k < bars ? '#' : '.';
    b[15] = 0;
    lcd_line(7, b, C_GREEN, C_BLACK);
}

static void show_status(const char *s, uint16_t fg)
{
    lcd_line(9, s, fg, C_BLACK);
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

/* "TX LOW T88.5", "TX MIN 1750" */
static void show_tx(void)
{
    char s[20] = "TX ";
    int p = 3;
    for (const char *n = power_name[power]; *n; n++) s[p++] = *n;
    s[p++] = ' ';
    if (tx_burst) { s[p++] = '1'; s[p++] = '7'; s[p++] = '5'; s[p++] = '0'; s[p] = 0; }
    else tone_text(s + p);
    show_status(s, C_RED);
}

static void show_entry(const char *e)
{
    char s[16] = "KHZ ";
    int p = 4;
    for (; *e && p < 15; e++) s[p++] = *e;
    s[p] = 0;
    lcd_line(10, s, C_YELLOW, C_BLACK);
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
        }
        char s[12];
        uart_puts("T "); uart_puts(tone_text(s) ? s : "OFF"); uart_puts("\r\n");
    } else if (cmd[0] == 'R' && cmd[1] == 'T' && (cmd[2] == ' ' || cmd[2] == 0)) {
        if (cmd[2] == ' ') {
            if (!parse_tone(cmd + 3, &rx_tone_type, &rx_tone_val)) {
                uart_puts("ERR RT OFF, RT 88.5 (CTCSS) or RT D023N / RT D023I (DCS)\r\n"); return;
            }
            if (!tx_on) rx_tone();
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
        }
        uart_puts("POW "); uart_puts(power_name[power]); uart_puts("\r\n");
    } else if (cmd[0] == 'M' && cmd[1] == 'I' && cmd[2] == 'C' && (cmd[3] == ' ' || cmd[3] == 0)) {
        int v;
        if (cmd[3] == ' ') {
            if (!parse_int(cmd + 4, &v) || v < 0 || v > 31) { uart_puts("ERR MIC 0-31\r\n"); return; }
            mic_gain = v;
            if (tx_on) bk1_write(0x7D, (uint16_t)(0xE940 + mic_gain));
        }
        uart_puts("MIC "); put_dec(mic_gain); uart_puts("\r\n");
    } else if (!__builtin_strcmp(cmd, "HELP")) {
        uart_puts("PING, VER, F [MHz], M FM|NFM|AM|USB, RSSI, SQ 0-9, VOL 0-63, MIC [0-31], POW [MIN|LOW|MID|HIGH], T [OFF|67.0|D023N|D023I], RT [same, receive], ADC, R1 xx, R2 xx. TX: PTT1 voice, alarm button 1750 Hz, amateur bands only.\r\n");
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
    backlight(255);
    lcd_line(0, "OPEN FW TEST 11", C_YELLOW, C_BLUE);

    radio_init();
    show_freq();
    show_status("RX", C_GREEN);
    show_entry("");
    report_freq();

    watchdog_start();

    char buf[32];
    int len = 0;
    char entry[8] = "";
    int elen = 0;
    uint16_t last_m = 0;
    int last_key = 0, tx_lockout = 0, alarm_count = 0;
    uint32_t tx_until = 0;
    uint32_t next_scan = 0, next_rssi = 0, next_blink = 0, next_sq = 0;
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
            int key = ptt || alarm;
            if (tx_on && (!key || (int32_t)(ticks - tx_until) >= 0)) {
                if (key) tx_lockout = 1;
                tx_stop();
                show_status(tx_lockout ? "RX (TOT 60 S)" : "RX", tx_lockout ? C_YELLOW : C_GREEN);
                uart_puts(tx_lockout ? "TX OFF TOT 60 S\r\n" : "TX OFF\r\n");
            } else if (tx_on && alarm != tx_burst) {
                tx_audio(alarm);
                show_tx();
                uart_puts(alarm ? "TX 1750\r\n" : "TX VOICE\r\n");
            } else if (key && !last_key && !tx_on && !tx_lockout) {
                if (tx_start(alarm)) {
                    tx_until = ticks + TX_MAX_MS;
                    show_tx();
                    uart_puts(alarm ? "TX ON 1750\r\n" : "TX ON\r\n");
                } else {
                    show_status("TX NOT ALLOWED", C_YELLOW);
                    uart_puts("TX REFUSED outside the amateur bands\r\n");
                }
            }
            if (!key) tx_lockout = 0;
            last_key = key;

            uint16_t m = keys_matrix();
            uint16_t pressed = m & (uint16_t)~last_m;
            last_m = m;
            for (int b = 0; b < 16 && !tx_on; b++) {
                if (!(pressed & (1u << b))) continue;
                char k = key_char[b];
                int changed = 0;
                if (k >= '0' && k <= '9') {
                    if (elen < 7) { entry[elen++] = k; entry[elen] = 0; }
                    show_entry(entry);
                } else if (k == K_EXIT) {
                    elen = 0; entry[0] = 0;
                    show_entry(entry);
                } else if (k == '#') {
                    uint32_t khz = 0;
                    for (int i = 0; i < elen; i++) khz = khz * 10 + (uint32_t)(entry[i] - '0');
                    elen = 0; entry[0] = 0;
                    show_entry(entry);
                    changed = radio_tune(khz * 100);
                    if (!changed) show_status("BAD FREQUENCY", C_YELLOW);
                } else if (k == '*') {
                    changed = radio_tune(44600625);          /* PMR446 channel 1, 446.00625 MHz */
                    if (changed) { mode = MODE_NFM; radio_mode(); }
                } else if (k == K_UP || k == K_DOWN) {
                    uint32_t f = k == K_UP ? freq10 + 1250 : freq10 - 1250;
                    changed = radio_tune(f);
                } else if (k == K_MENU) {
                    mode = (mode + 1) % NMODES;
                    audio_close();
                    radio_mode();
                    changed = 1;
                }
                if (changed) {
                    show_freq();
                    show_status("RX", C_GREEN);
                    report_freq();
                }
            }
        }

        /* Squelch every 50 ms; side keys force it open. */
        if (!tx_on && (int32_t)(ticks - next_sq) >= 0) {
            next_sq = ticks + 50;
            last_rssi = rssi_dbm();
            last_noise = bk_read(0, 0x65) & 0x7F;
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
        }

        if (!tx_on && (int32_t)(ticks - next_rssi) >= 0) {
            next_rssi = ticks + 300;
            show_rssi(last_rssi, last_noise);
        }

        if (!tx_on && (int32_t)(ticks - next_blink) >= 0) {
            next_blink = ticks + 500;
            GPIOA->odt ^= GPIO_PINS_14;
        }
    }
}
