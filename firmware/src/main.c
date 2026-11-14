/* UV-98 Plus / RT-880G open firmware, test 8: receive audio, squelch, side keys, PMR446 voice TX at minimum power.
 * Pin map and stock sequences: ../FINDINGS.md. Only BK4819 #1 is written; #2 is read only. */
#include "at32f423.h"
#include "font.h"

#define FW_NAME  "UV98+ open test 8"
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

/* Chip 1 only. */
static void bk1_write(uint8_t reg, uint16_t val)
{
    const bkbus_t *b = &bk[0];
    gpio_bits_reset(b->ck_port, b->ck);
    gpio_bits_reset(b->cs_port, b->cs);
    bk_byte(b, reg & 0x7F);
    bk_byte(b, (uint8_t)(val >> 8));
    bk_byte(b, (uint8_t)val);
    gpio_bits_set(b->cs_port, b->cs);
}

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
/* MCU amplifier pins PC15/PB11 low; BK4819 #1: PA power 0, its GPIO outputs (0x33) 0, RF off. */
static void tx_kill(void)
{
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    bk1_write(0x36, 0x007F);
    bk1_write(0x33, 0x0000);
    bk1_write(0x30, 0x0000);
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

/* PMR446: 446.00625-446.19375 MHz. The only range test 4 transmits in. */
#define PMR_LO 44600000u
#define PMR_HI 44620000u
/* Minimum power for testing: BK4819 PA level 8, external UHF amplifier (0x33 bit 2) left off.
 * (Calibrated low at 446 MHz is level 0x16 with the amplifier on.) */
#define TX_POWER_REG ((8u << 10) | 0xFF)
#define TX_MIC_GAIN  0xE952   /* 0x7D: 0xE940 + this radio's mic gain setting (0xAD = 0x12) */
#define TX_DEVIATION 0x34E2   /* 400-480 MHz calibration block, offset 3 */
#define TX_MAX_MS    5000

enum { MODE_FM, MODE_NFM, MODE_AM };
static const char *const mode_name[] = { "FM", "NFM", "AM" };

static uint32_t freq10 = 14580000;   /* 145.800 MHz */
static int mode = MODE_FM;
static int tx_on;

static const band_t *band_of(uint32_t f)
{
    for (unsigned i = 0; i < NBANDS; i++)
        if (f >= bands[i].lo && f < bands[i].hi) return &bands[i];
    return 0;
}

/* Filter bandwidth; audio stays muted until the squelch opens it. */
static void radio_mode(void)
{
    bk1_write(0x43, mode == MODE_NFM ? 0x4048 : 0x3028);
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
    radio_mode();
    radio_tune(freq10);
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
    return (uint16_t)(0xB000 | ((volume & 0x3F) << 4) | (mode == MODE_AM ? 0xC : 0xF));
}

static void audio_open(void)
{
    bk1_write(0x48, rx_gain());
    bk1_write(0x47, (uint16_t)(0x6040 | ((mode == MODE_AM ? 7 : 1) << 8)));
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

static int in_pmr(uint32_t f) { return f >= PMR_LO && f < PMR_HI; }

/* Voice TX (stock 0x800B22C -> 0x80053F0 / 0x80052D4 with 0x30 = 0xC1FE), PMR446 only,
 * minimum power. Red LED (PA14) on, green (PA13) off. */
static int tx_start(void)
{
    const band_t *b = band_of(freq10);
    if (!b || !in_pmr(freq10)) return 0;
    audio_close();
    gpio_bits_reset(GPIOA, GPIO_PINS_13);
    gpio_bits_set(GPIOA, GPIO_PINS_14);
    bk1_write(0x33, bk_read(0, 0x33) & 0xFFC0);                 /* RX filters and TX amps off */
    if (b->tx_var == 3 || b->tx_var == 4) gpio_bits_reset(GPIOF, GPIO_PINS_0);
    else gpio_bits_set(GPIOF, GPIO_PINS_0);
    bk1_write(0x40, TX_DEVIATION);
    bk1_write(0x7D, TX_MIC_GAIN);
    bk1_write(0x70, 0x0000);                                     /* no tone */
    bk1_write(0x47, 0x6040 | (1 << 8));                          /* AF: FM (microphone) */
    bk1_write(0x37, 0x1D0F);
    delay_ms(10);
    bk1_write(0x52, 0x028F);
    bk1_write(0x30, 0x0200);
    bk1_write(0x30, 0xC1FE);
    gpio_bits_reset(GPIOC, GPIO_PINS_15);
    gpio_bits_reset(GPIOB, GPIO_PINS_11);
    bk1_write(0x36, TX_POWER_REG);
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
    bk1_write(0x30, 0x0200);
    tx_on = 0;
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
        for (int i = 0; i < 3; i++) if (!__builtin_strcmp(m, mode_name[i])) n = i;
        if (n < 0) { uart_puts("ERR mode FM, NFM or AM\r\n"); return; }
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
    } else if (!__builtin_strcmp(cmd, "HELP")) {
        uart_puts("PING, VER, F [MHz], M FM|NFM|AM, RSSI, SQ 0-9, VOL 0-63, ADC, R1 xx, R2 xx. TX: PTT1 on PMR446 only.\r\n");
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
    lcd_line(0, "OPEN FW TEST 8", C_YELLOW, C_BLUE);

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
    int last_ptt = 0, tx_lockout = 0;
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

            int ptt = ptt_down();
            if (tx_on && (!ptt || (int32_t)(ticks - tx_until) >= 0)) {
                if (ptt) tx_lockout = 1;
                tx_stop();
                show_status(tx_lockout ? "RX (5 S LIMIT)" : "RX", tx_lockout ? C_YELLOW : C_GREEN);
                uart_puts(tx_lockout ? "TX OFF 5 S LIMIT\r\n" : "TX OFF\r\n");
            } else if (ptt && !last_ptt && !tx_on && !tx_lockout) {
                if (tx_start()) {
                    tx_until = ticks + TX_MAX_MS;
                    show_status("TX MIN POWER", C_RED);
                    uart_puts("TX ON\r\n");
                } else {
                    show_status("TX PMR446 ONLY", C_YELLOW);
                    uart_puts("TX REFUSED outside PMR446\r\n");
                }
            }
            if (!ptt) tx_lockout = 0;
            last_ptt = ptt;

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
                    mode = (mode + 1) % 3;
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
            int carrier = mode == MODE_AM
                ? last_rssi >= (audio_on ? open_rssi - 3 : open_rssi)
                : last_noise <= (audio_on ? open_noise + 10 : open_noise);
            int want = sq_level == 0 || monitor || carrier;
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
