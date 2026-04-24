////#include <stdint.h>
////#include <stdbool.h>
////#include <string.h>
////
////#include "tm4c123gh6pm.h"
////#include "hw_memmap.h"
////#include "pin_map.h"
////
////#include "sysctl.h"
////#include "gpio.h"
////#include "uart.h"
////#include "interrupt.h"
////#include "adc.h"
////#include "watchdog.h"
////#include "systick.h"
////
////// =============================================================================
////// Constants
////// =============================================================================
////#define RX_BUF_SIZE         256
////#define CMD_BUF_SIZE        32
////
////// Pin assignments
////#define DHT11_PORT          GPIO_PORTE_BASE
////#define DHT11_PIN           GPIO_PIN_3      // PE3
////
////#define LDR_PORT            GPIO_PORTE_BASE
////#define LDR_PIN             GPIO_PIN_2      // PE2 — AIN1
////
////#define FAN_PORT            GPIO_PORTC_BASE
////#define FAN_PIN             GPIO_PIN_4      // PC4
////
////#define BUZZER_PORT         GPIO_PORTC_BASE
////#define BUZZER_PIN          GPIO_PIN_5      // PC5
////
////#define PIR_PORT            GPIO_PORTD_BASE
////#define PIR_PIN             GPIO_PIN_0      // PD0
////
////// Timing
//////   SysCtlDelay(N) = 3N CPU cycles @ 16 MHz
//////   1 us  ≈ SysCtlDelay(5)   (16 cycles / 3 ≈ 5)
//////   18 ms ≈ SysCtlDelay(96000)
//////   40 us ≈ SysCtlDelay(213)
//////   WDT reload: 16e6 * 3 = 3-second timeout
////#define DHT11_START_DELAY   96000UL         // 18 ms pull-low start pulse
////#define DHT11_RELEASE_DELAY 160UL           // 30 us release to input
////#define DHT11_BIT_THRESHOLD 213UL           // 40 us sample threshold
////#define DHT11_MIN_PERIOD_MS 2000UL          // min ms between DHT11 reads
////#define DHT11_TIMEOUT       300UL           // loop iteration timeout per phase
////
////#define WDT_RELOAD_CYCLES   (16000000UL * 3)
////#define POLL_INTERVAL_MS    5000UL          // sensor auto-poll period
////
////// Defaults
////#define FAN_THRESH_DEFAULT  80              // F — fan turns on above this
////#define LDR_DARK_THRESH     70              // % brightness — below = dark room
////#define MOTION_COOLDOWN_MS  5000UL          // min ms between motion events
////
////// =============================================================================
////// Global state
////// =============================================================================
////
////// UART ring buffer (filled by ISR)
////volatile char     rx_buf[RX_BUF_SIZE];
////volatile uint16_t rx_head = 0;
////volatile uint16_t rx_tail = 0;
////
////// Millisecond tick (incremented by SysTick ISR)
////volatile uint32_t g_tick_ms = 0;
////
////// Connection state
////static bool g_connected = true;
////
////// RGB state (mirrors physical pin state)
////static uint8_t g_r = 0, g_g = 0, g_b = 0;
////
////// DHT11 cached readings
////static int16_t  g_dht_temp_f    = 0;       // last valid temp in whole °F
////static uint8_t  g_dht_humid     = 0;       // last valid humidity %
////static bool     g_dht_valid     = false;   // true once first successful read
////static uint32_t g_dht_last_ms   = 0;       // tick of last successful read
////
////// Fan state
////static bool    g_fan_on     = false;
////static bool    g_fan_auto   = true;
////static uint8_t g_fan_thresh = FAN_THRESH_DEFAULT;
////
////// Motion state
////static bool          g_motion_armed = true;
////static volatile bool g_motion_evt   = false;
////
////// Buzzer state
////static volatile uint32_t g_buzz_until_ms = 0;
////
////// LDR / lighting state
////static bool    g_ldr_auto   = true;
////static uint8_t g_ldr_manual = 100;
////
////// Periodic poll tracking
////static uint32_t g_last_poll_ms   = 0;
////static uint32_t g_motion_last_ms = 0;
////
////// Reset cause
////static uint32_t g_reset_cause = 0;
////
////// Forward declarations
////void UART1_Handler(void);
////void GPIOD_Handler(void);
////void SysTick_Handler(void);
////void WDT0_Handler(void);
////
////// =============================================================================
////// UART output helpers
////// =============================================================================
////
////static void uart1_put_u32(uint32_t v)
////{
////    char tmp[11];
////    int  i = 0;
////
////    if (v == 0) {
////        UARTCharPut(UART1_BASE, '0');
////        return;
////    }
////    while (v > 0 && i < 10) {
////        tmp[i++] = (char)('0' + (v % 10));
////        v /= 10;
////    }
////    while (i--)
////        UARTCharPut(UART1_BASE, tmp[i]);
////}
////
////static void uart1_puts(const char *s)
////{
////    while (*s)
////        UARTCharPut(UART1_BASE, *s++);
////}
////
////static char bit_char(uint8_t v) { return v ? '1' : '0'; }
////
////static uint8_t parse_u8(const char *s)
////{
////    uint16_t v = 0;
////    while (*s >= '0' && *s <= '9') {
////        v = v * 10 + (uint16_t)(*s++ - '0');
////        if (v > 255) return 255;
////    }
////    return (uint8_t)v;
////}
////
////// Write a signed integer over UART (used for °F values)
////static void uart1_put_i16(int16_t v)
////{
////    if (v < 0) {
////        UARTCharPut(UART1_BASE, '-');
////        uart1_put_u32((uint32_t)(-v));
////    } else {
////        uart1_put_u32((uint32_t)v);
////    }
////}
////
////// =============================================================================
////// RGB LED
////// =============================================================================
////
////static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
////{
////    g_r = r ? 1 : 0;
////    g_g = g ? 1 : 0;
////    g_b = b ? 1 : 0;
////
////    uint8_t val = 0;
////    if (g_r) val |= GPIO_PIN_1; // Red   — PF1
////    if (g_b) val |= GPIO_PIN_2; // Blue  — PF2
////    if (g_g) val |= GPIO_PIN_3; // Green — PF3
////
////    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, val);
////}
////
////// =============================================================================
////// DHT11 — bit-bang 1-wire protocol on PE3
////// =============================================================================
////// Protocol summary:
//////   1. MCU pulls data line LOW for 18 ms (start signal)
//////   2. MCU releases line (external 10kΩ pulls HIGH)
//////   3. DHT11 responds: 80 us LOW, 80 us HIGH
//////   4. 40 data bits follow, each bit = 50 us LOW + (26-28 us=0 | 70 us=1) HIGH
//////   5. 5 bytes: humidity_int, humidity_dec, temp_int, temp_dec, checksum
//////
////// Interrupts are disabled for the duration of the read (~5 ms) to prevent
////// SysTick from corrupting the microsecond-level timing.
////// =============================================================================
////
////static void dht11_pin_output(void)
////{
////    GPIOPinTypeGPIOOutput(DHT11_PORT, DHT11_PIN);
////}
////
////static void dht11_pin_input(void)
////{
////    GPIOPinTypeGPIOInput(DHT11_PORT, DHT11_PIN);
////    // Rely on external 10kΩ pull-up — do not enable internal pull-up
////    GPIOPadConfigSet(DHT11_PORT, DHT11_PIN,
////                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
////}
////
////static uint8_t dht11_pin_read(void)
////{
////    return (GPIOPinRead(DHT11_PORT, DHT11_PIN) & DHT11_PIN) ? 1 : 0;
////}
////
////// Returns 0 on success, -1 on timeout or checksum failure.
////// On success, updates g_dht_temp_f, g_dht_humid, g_dht_valid, g_dht_last_ms.
////static int dht11_read(void)
////{
////    uint8_t  data[5] = {0};
////    uint32_t count;
////    int      bit;
////
////    // ---- Send start signal -----------------------------------------------
////    IntMasterDisable();         // disable all interrupts for timing accuracy
////
////    dht11_pin_output();
////    GPIOPinWrite(DHT11_PORT, DHT11_PIN, 0);     // pull LOW
////    SysCtlDelay(DHT11_START_DELAY);             // hold 18 ms
////
////    GPIOPinWrite(DHT11_PORT, DHT11_PIN, DHT11_PIN); // release HIGH
////    SysCtlDelay(DHT11_RELEASE_DELAY);           // wait 30 us
////
////    dht11_pin_input();                          // switch to input
////
////    // ---- Wait for DHT11 response LOW ------------------------------------
////    count = 0;
////    while (dht11_pin_read()) {
////        SysCtlDelay(5);                         // ~1 us per iteration
////        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
////    }
////
////    // ---- Wait through DHT11's 80 us LOW ---------------------------------
////    count = 0;
////    while (!dht11_pin_read()) {
////        SysCtlDelay(5);
////        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
////    }
////
////    // ---- Wait through DHT11's 80 us HIGH --------------------------------
////    count = 0;
////    while (dht11_pin_read()) {
////        SysCtlDelay(5);
////        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
////    }
////
////    // ---- Read 40 data bits ----------------------------------------------
////    // Each bit: 50 us LOW, then 26-28 us HIGH (0) or 70 us HIGH (1)
////    // Strategy: wait for HIGH, wait 40 us, sample. HIGH=1, LOW=0.
////    for (bit = 0; bit < 40; bit++) {
////        // Wait for line to go HIGH (end of 50 us low period)
////        count = 0;
////        while (!dht11_pin_read()) {
////            SysCtlDelay(5);
////            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
////        }
////
////        // Wait 40 us — threshold between 0-bit (~27 us high) and 1-bit (~70 us high)
////        SysCtlDelay(DHT11_BIT_THRESHOLD);
////
////        // Sample and shift into data byte
////        data[bit / 8] <<= 1;
////        if (dht11_pin_read())
////            data[bit / 8] |= 1;
////
////        // Wait for line to go LOW before next bit's 50 us low period
////        count = 0;
////        while (dht11_pin_read()) {
////            SysCtlDelay(5);
////            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
////        }
////    }
////
////    IntMasterEnable();          // re-enable interrupts
////
////    // ---- Verify checksum ------------------------------------------------
////    // checksum = (hum_int + hum_dec + tmp_int + tmp_dec) & 0xFF
////    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
////        return -1;
////
////    // ---- Update cached state --------------------------------------------
////    // data[0] = humidity integer part (%), data[2] = temperature integer (C)
////    // DHT11 decimal bytes (data[1], data[3]) are always 0
////    uint8_t temp_c  = data[2];
////    uint8_t humid   = data[0];
////
////    g_dht_temp_f  = (int16_t)((temp_c * 9) / 5 + 32);  // C to F (integer)
////    g_dht_humid   = humid;
////    g_dht_valid   = true;
////    g_dht_last_ms = g_tick_ms;
////
////    return 0;
////}
////
////// Reads fresh data if >2 s have elapsed since last read, otherwise returns
////// cached values. Returns false if no valid data is available.
////static bool dht11_read_cached(void)
////{
////    if (!g_dht_valid || (g_tick_ms - g_dht_last_ms >= DHT11_MIN_PERIOD_MS))
////        dht11_read();      // attempt fresh read; on failure g_dht_valid stays false
////    return g_dht_valid;
////}
////
////// =============================================================================
////// ADC — KY-018 LDR only (AIN1 / PE2), sequencer 3 single-sample
////// =============================================================================
////
////static void adc0_init(void)
////{
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)) {}
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
////
////    // PE2 = AIN1 (LDR).  PE3 is DHT11 GPIO — do NOT configure as ADC.
////    GPIOPinTypeADC(LDR_PORT, LDR_PIN);
////
////    // Sequencer 3: single-sample, processor-triggered, highest priority
////    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
////    ADCSequenceStepConfigure(ADC0_BASE, 3, 0,
////                             ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
////    ADCSequenceEnable(ADC0_BASE, 3);
////    ADCIntClear(ADC0_BASE, 3);
////}
////
////// Returns raw 12-bit LDR sample
////static uint32_t adc0_read_ldr(void)
////{
////    uint32_t result = 0;
////    ADCProcessorTrigger(ADC0_BASE, 3);
////    while (!ADCIntStatus(ADC0_BASE, 3, false)) {}
////    ADCIntClear(ADC0_BASE, 3);
////    ADCSequenceDataGet(ADC0_BASE, 3, &result);
////    return result;
////}
////
////// KY-018: bright room -> low raw -> high brightness %
////// Returns 0 (dark) to 100 (bright)
////static uint8_t adc_to_light_pct(uint32_t raw)
////{
////    uint32_t pct = 100UL - (raw * 100UL / 4095UL);
////    return (uint8_t)(pct > 100 ? 100 : pct);
////}
////
////// =============================================================================
////// PIR motion sensor — PD0, rising-edge interrupt
////// =============================================================================
////
////static void pir_init(void)
////{
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}
////
////    GPIOPinTypeGPIOInput(PIR_PORT, PIR_PIN);
////    GPIOPadConfigSet(PIR_PORT, PIR_PIN,
////                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
////    GPIOIntTypeSet(PIR_PORT, PIR_PIN, GPIO_RISING_EDGE);
////    GPIOIntRegister(PIR_PORT, GPIOD_Handler);
////    GPIOIntEnable(PIR_PORT, PIR_PIN);
////}
////
////void GPIOD_Handler(void)
////{
////    uint32_t status = GPIOIntStatus(PIR_PORT, true);
////    GPIOIntClear(PIR_PORT, status);
////    if ((status & PIR_PIN) && g_motion_armed)
////        g_motion_evt = true;
////}
////
////// =============================================================================
////// Active buzzer — PC5, GPIO HIGH = on
////// =============================================================================
////
////static void buzzer_init(void)
////{
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
////    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
////    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
////}
////
////static void buzzer_set(bool on)
////{
////    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, on ? BUZZER_PIN : 0);
////}
////
////// =============================================================================
////// Fan — PC4, GPIO HIGH = on  [STUB: transistor not yet wired]
////// =============================================================================
////
////static void fan_init(void)
////{
////    // GPIOC already enabled by buzzer_init
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
////    GPIOPinTypeGPIOOutput(FAN_PORT, FAN_PIN);
////    GPIOPinWrite(FAN_PORT, FAN_PIN, 0);
////}
////
////static void fan_set(bool on)
////{
////    g_fan_on = on;
////    GPIOPinWrite(FAN_PORT, FAN_PIN, on ? FAN_PIN : 0);
////}
////
////// =============================================================================
////// Hardware Watchdog — WDT0, 3-second timeout → reset
////// =============================================================================
////
////static void watchdog_init(void)
////{
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0)) {}
////    WatchdogReloadSet(WATCHDOG0_BASE, WDT_RELOAD_CYCLES);
////    WatchdogIntRegister(WATCHDOG0_BASE, WDT0_Handler);
////    WatchdogResetEnable(WATCHDOG0_BASE);
////    WatchdogEnable(WATCHDOG0_BASE);
////}
////
////void WDT0_Handler(void)
////{
////    // Intentionally empty — second timeout triggers hardware reset
////}
////
////static void watchdog_pet(void)
////{
////    WatchdogIntClear(WATCHDOG0_BASE);
////}
////
////// =============================================================================
////// SysTick — 1 ms tick counter
////// =============================================================================
////
////void SysTick_Handler(void)
////{
////    g_tick_ms++;
////}
////
////static void systick_init(void)
////{
////    SysTickPeriodSet(SysCtlClockGet() / 1000);  // 16000 cycles = 1 ms @ 16 MHz
////    SysTickIntRegister(SysTick_Handler);
////    SysTickIntEnable();
////    SysTickEnable();
////}
////
////// =============================================================================
////// UART1 — HC-05 Bluetooth, 9600 8N1, interrupt-driven RX
////// =============================================================================
////
////static void uart1_init_9600(void)
////{
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART1)) {}
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}
////
////    GPIOPinConfigure(GPIO_PB0_U1RX);
////    GPIOPinConfigure(GPIO_PB1_U1TX);
////    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
////
////    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 9600,
////                        UART_CONFIG_WLEN_8  |
////                        UART_CONFIG_STOP_ONE |
////                        UART_CONFIG_PAR_NONE);
////    UARTEnable(UART1_BASE);
////
////    UARTIntRegister(UART1_BASE, UART1_Handler);
////    UARTIntEnable(UART1_BASE, UART_INT_RX | UART_INT_RT);
////}
////
////void UART1_Handler(void)
////{
////    uint32_t status = UARTIntStatus(UART1_BASE, true);
////    UARTIntClear(UART1_BASE, status);
////    while (UARTCharsAvail(UART1_BASE)) {
////        char     c    = (char)UARTCharGetNonBlocking(UART1_BASE);
////        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
////        if (next != rx_tail) {
////            rx_buf[rx_head] = c;
////            rx_head = next;
////        }
////    }
////}
////
////static int read_line_uart1(char *buf, int max_len)
////{
////    static int i = 0;
////    while (rx_tail != rx_head) {
////        char c = rx_buf[rx_tail];
////        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
////        if (c == '\r' || c == '\n') {
////            if (i > 0) { buf[i] = '\0'; i = 0; return 1; }
////            continue;
////        }
////        if (i < max_len - 1)
////            buf[i++] = c;
////    }
////    return 0;
////}
////
////// =============================================================================
////// Periodic auto sensor logic — runs every POLL_INTERVAL_MS from main loop
////// =============================================================================
////
////static void run_auto_logic(void)
////{
////    // --- DHT11: refresh cached temp + humidity ---------------------------
////    dht11_read_cached();
////
////    // --- Fan auto control (uses integer °F from DHT11) -------------------
////    if (g_fan_auto && g_dht_valid) {
////        int16_t temp     = g_dht_temp_f;
////        int16_t thresh   = (int16_t)g_fan_thresh;
////        int16_t hyst     = thresh - 2;         // 2°F hysteresis band
////
////        if (!g_fan_on && temp >= thresh) {
////            fan_set(true);
////            uart1_puts("EVT FAN_ON TEMP=");
////            uart1_put_i16(g_dht_temp_f);
////            uart1_puts("F\r\n");
////        } else if (g_fan_on && temp < hyst) {
////            fan_set(false);
////            uart1_puts("EVT FAN_OFF\r\n");
////        }
////    }
////
////    // --- LDR auto-dim (onboard blue LED placeholder) ---------------------
////    if (g_ldr_auto) {
////        uint8_t brightness = adc_to_light_pct(adc0_read_ldr());
////        if (brightness < LDR_DARK_THRESH)
////            set_rgb(g_r, g_g, 1);
////        else
////            set_rgb(g_r, g_g, 0);
////    }
////}
////
////// =============================================================================
////// Command parser
////// =============================================================================
////
////static void handle_cmd(const char *cmd)
////{
////    // --- Diagnostics -------------------------------------------------------
////
////    if (!strcmp(cmd, "PING")) {
////        uart1_puts("OK PONG\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "VERSION")) {
////        uart1_puts("OK FW 0.4\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "UPTIME")) {
////        uart1_puts("OK UPTIME=");
////        uart1_put_u32(g_tick_ms / 1000);
////        uart1_puts("s\r\n");
////        return;
////    }
////
////    // --- Full state snapshot -----------------------------------------------
////    // Format: OK RGB=xyz TEMP=XXF HUMID=XX% FAN=0 MODE=AUTO THRESH=80
////    //                MOTION=ARMED LIGHT=63 LDR=AUTO
////
////    if (!strcmp(cmd, "STATE")) {
////        dht11_read_cached();
////
////        uart1_puts("OK RGB=");
////        UARTCharPut(UART1_BASE, bit_char(g_r));
////        UARTCharPut(UART1_BASE, bit_char(g_g));
////        UARTCharPut(UART1_BASE, bit_char(g_b));
////
////        uart1_puts(" TEMP=");
////        if (g_dht_valid) {
////            uart1_put_i16(g_dht_temp_f);
////            UARTCharPut(UART1_BASE, 'F');
////        } else {
////            uart1_puts("ERR");
////        }
////
////        uart1_puts(" HUMID=");
////        if (g_dht_valid) {
////            uart1_put_u32(g_dht_humid);
////            UARTCharPut(UART1_BASE, '%');
////        } else {
////            uart1_puts("ERR");
////        }
////
////        uart1_puts(" FAN=");
////        UARTCharPut(UART1_BASE, g_fan_on ? '1' : '0');
////
////        uart1_puts(" MODE=");
////        uart1_puts(g_fan_auto ? "AUTO" : "MANUAL");
////
////        uart1_puts(" THRESH=");
////        uart1_put_u32(g_fan_thresh);
////
////        uart1_puts(" MOTION=");
////        uart1_puts(g_motion_armed ? "ARMED" : "DISARMED");
////
////        uart1_puts(" LIGHT=");
////        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
////
////        uart1_puts(" LDR=");
////        uart1_puts(g_ldr_auto ? "AUTO" : "MANUAL");
////
////        uart1_puts("\r\n");
////        return;
////    }
////
////    // --- Temperature -------------------------------------------------------
////
////    if (!strcmp(cmd, "TEMP")) {
////        dht11_read_cached();
////        uart1_puts("OK TEMP=");
////        if (g_dht_valid) {
////            uart1_put_i16(g_dht_temp_f);
////            uart1_puts("F\r\n");
////        } else {
////            uart1_puts("ERR\r\n");
////        }
////        return;
////    }
////
////    // --- Humidity ----------------------------------------------------------
////
////    if (!strcmp(cmd, "HUMID")) {
////        dht11_read_cached();
////        uart1_puts("OK HUMID=");
////        if (g_dht_valid) {
////            uart1_put_u32(g_dht_humid);
////            uart1_puts("%\r\n");
////        } else {
////            uart1_puts("ERR\r\n");
////        }
////        return;
////    }
////
////    // --- Light level -------------------------------------------------------
////
////    if (!strcmp(cmd, "LIGHT")) {
////        uart1_puts("OK LIGHT=");
////        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
////        uart1_puts("\r\n");
////        return;
////    }
////
////    // --- Fan control -------------------------------------------------------
////
////    if (!strcmp(cmd, "FAN1")) {
////        g_fan_auto = false;
////        fan_set(true);
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "FAN0")) {
////        g_fan_auto = false;
////        fan_set(false);
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "FAN_AUTO")) {
////        g_fan_auto = true;
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (strncmp(cmd, "FANTHRESH:", 10) == 0) {
////        uint8_t t = parse_u8(cmd + 10);
////        if (t >= 60 && t <= 100) {
////            g_fan_thresh = t;
////            uart1_puts("OK THRESH=");
////            uart1_put_u32(g_fan_thresh);
////            uart1_puts("\r\n");
////        } else {
////            uart1_puts("ERR RANGE 60-100\r\n");
////        }
////        return;
////    }
////
////    // --- Motion sensor -----------------------------------------------------
////
////    if (!strcmp(cmd, "MOTION_ARM")) {
////        g_motion_armed = true;
////        uart1_puts("OK MOTION=ARMED\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "MOTION_DISARM")) {
////        g_motion_armed = false;
////        uart1_puts("OK MOTION=DISARMED\r\n");
////        return;
////    }
////
////    // --- Buzzer ------------------------------------------------------------
////
////    if (strncmp(cmd, "BUZZ:", 5) == 0) {
////        uint8_t dur = parse_u8(cmd + 5);
////        if (dur > 0)
////            g_buzz_until_ms = g_tick_ms + (uint32_t)dur * 100;
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "BUZZ0")) {
////        g_buzz_until_ms = 0;
////        buzzer_set(false);
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    // --- LDR / lighting ----------------------------------------------------
////
////    if (!strcmp(cmd, "LDR_AUTO")) {
////        g_ldr_auto = true;
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (strncmp(cmd, "LDR_MAN:", 8) == 0) {
////        uint8_t pct = parse_u8(cmd + 8);
////        if (pct > 100) pct = 100;
////        g_ldr_auto   = false;
////        g_ldr_manual = pct;
////        set_rgb(g_r, g_g, pct >= 50 ? 1 : 0);
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    // --- RGB LED -----------------------------------------------------------
////
////    if (cmd[0] == 'X' && cmd[1] == '\0') {
////        set_rgb(0, 0, 0);
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if (cmd[0] == 'R' && cmd[1] == 'G' && cmd[2] == 'B' && cmd[3] == ':' &&
////        (cmd[4] == '0' || cmd[4] == '1') &&
////        (cmd[5] == '0' || cmd[5] == '1') &&
////        (cmd[6] == '0' || cmd[6] == '1') &&
////        cmd[7] == '\0')
////    {
////        set_rgb(cmd[4] == '1', cmd[5] == '1', cmd[6] == '1');
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    if ((cmd[0] == 'R' || cmd[0] == 'G' || cmd[0] == 'B') &&
////        (cmd[1] == '0' || cmd[1] == '1') &&
////        cmd[2] == '\0')
////    {
////        uint8_t on = (cmd[1] == '1');
////        if (cmd[0] == 'R') set_rgb(on,  g_g, g_b);
////        if (cmd[0] == 'G') set_rgb(g_r, on,  g_b);
////        if (cmd[0] == 'B') set_rgb(g_r, g_g, on );
////        uart1_puts("OK\r\n");
////        return;
////    }
////
////    // --- Session -----------------------------------------------------------
////
////    if (!strcmp(cmd, "HELP")) {
////        uart1_puts("OK PING VERSION UPTIME STATE TEMP HUMID LIGHT "
////                   "FAN0 FAN1 FAN_AUTO FANTHRESH:XX "
////                   "MOTION_ARM MOTION_DISARM "
////                   "BUZZ:X BUZZ0 "
////                   "LDR_AUTO LDR_MAN:XX "
////                   "RGB:xyz R0/R1 G0/G1 B0/B1 X EXIT\r\n");
////        return;
////    }
////
////    if (!strcmp(cmd, "EXIT")) {
////        uart1_puts("OK DISCONNECTED\r\n");
////        g_connected = false;
////        return;
////    }
////
////    uart1_puts("ERR\r\n");
////}
////
////// =============================================================================
////// main
////// =============================================================================
////
////int main(void)
////{
////    // --- Clock: 16 MHz main oscillator ------------------------------------
////    SysCtlClockSet(SYSCTL_SYSDIV_1  |
////                   SYSCTL_USE_OSC   |
////                   SYSCTL_OSC_MAIN  |
////                   SYSCTL_XTAL_16MHZ);
////
////    g_reset_cause = SysCtlResetCauseGet();
////    SysCtlResetCauseClear(g_reset_cause);
////
////    // --- Peripheral init --------------------------------------------------
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
////    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE,
////                          GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
////    set_rgb(0, 0, 0);
////
////    // GPIOE: ADC needs PE2 (LDR), DHT11 needs PE3 — both enabled in adc0_init
////    adc0_init();
////
////    // DHT11 starts as input with external pull-up holding line HIGH (idle state)
////    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
////    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
////    dht11_pin_input();
////
////    uart1_init_9600();
////    pir_init();
////    buzzer_init();
////    fan_init();
////    systick_init();
////
////    IntMasterEnable();
////
////    // --- HC-05 boot delay (blue LED = waiting) ----------------------------
////    set_rgb(0, 0, 1);
////    SysCtlDelay(16000000UL * 2 / 3);   // ~2 s @ 16 MHz
////    set_rgb(0, 0, 0);
////
////    // --- Watchdog armed after boot delay ----------------------------------
////    watchdog_init();
////
////    // --- Startup message --------------------------------------------------
////    if (g_reset_cause & SYSCTL_CAUSE_WDOG0)
////        uart1_puts("EVT WDT_RESET\r\n");
////
////    uart1_puts("SYSTEM STATUS: READY\r\n"
////               "FW 0.4 | DHT11 | ENTER A COMMAND OR 'HELP'\r\n");
////
////    // Initial sensor read — sent to host immediately on connect
////    // DHT11 needs a moment after power-on before first read is reliable
////    SysCtlDelay(16000000UL / 3);       // 1 s additional DHT11 settle time
////    if (dht11_read() == 0) {
////        uart1_puts("STARTUP TEMP=");
////        uart1_put_i16(g_dht_temp_f);
////        uart1_puts("F HUMID=");
////        uart1_put_u32(g_dht_humid);
////        uart1_puts("% LIGHT=");
////        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
////        uart1_puts("\r\n");
////    } else {
////        uart1_puts("STARTUP TEMP=ERR HUMID=ERR LIGHT=");
////        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
////        uart1_puts("\r\n");
////    }
////
////    // --- Main loop --------------------------------------------------------
////    char line[CMD_BUF_SIZE];
////
////    while (1)
////    {
////        watchdog_pet();
////
////        // Motion event
////        if (g_motion_evt) {
////            g_motion_evt = false;
////            if (g_tick_ms - g_motion_last_ms >= MOTION_COOLDOWN_MS) {
////                g_motion_last_ms = g_tick_ms;
////                uart1_puts("EVT MOTION\r\n");
////                g_buzz_until_ms = g_tick_ms + 300;
////            }
////        }
////
////        // Buzzer timer
////        if (g_buzz_until_ms > 0) {
////            if (g_tick_ms < g_buzz_until_ms)
////                buzzer_set(true);
////            else {
////                buzzer_set(false);
////                g_buzz_until_ms = 0;
////            }
////        }
////
////        // Periodic auto logic
////        if (g_tick_ms - g_last_poll_ms >= POLL_INTERVAL_MS) {
////            g_last_poll_ms = g_tick_ms;
////            run_auto_logic();
////        }
////
////        // UART command handler
////        if (g_connected && read_line_uart1(line, sizeof(line)))
////            handle_cmd(line);
////    }
////}
//
//
//
//
//
//
//
//// =============================================================================
//// TM4C123G Smart Home Controller — Firmware v0.4
//// =============================================================================
//// Peripherals:
////   DHT11   — PE3 (GPIO)  : temperature + humidity sensor (1-wire bit-bang)
////   KY-018  — PE2 (AIN1)  : photoresistor / ambient light (ADC sequencer 3)
////   HC-SR501— PD0         : PIR motion sensor (rising-edge interrupt)
////   Buzzer  — PC5         : active buzzer (GPIO HIGH = on)
////   Fan     — PC4         : DC fan via 2N2222A (GPIO HIGH = on) [STUB]
////   HC-05   — PB0/PB1     : Bluetooth UART1 (9600 8N1)
////   RGB LED — PF1/PF2/PF3 : onboard RGB LED (R/B/G)
////
//// DHT11 wiring:
////   DATA → PE3  +  10kΩ pull-up between PE3 and 3.3V
////   VCC  → 3.3V
////   GND  → GND
//// =============================================================================
//
//#include <stdint.h>
//#include <stdbool.h>
//#include <string.h>
//
//#include "tm4c123gh6pm.h"
//#include "hw_memmap.h"
//#include "pin_map.h"
//
//#include "sysctl.h"
//#include "gpio.h"
//#include "uart.h"
//#include "interrupt.h"
//#include "adc.h"
//#include "watchdog.h"
//#include "systick.h"
//
//// =============================================================================
//// Constants
//// =============================================================================
//#define RX_BUF_SIZE         256
//#define CMD_BUF_SIZE        32
//
//// Pin assignments
//#define DHT11_PORT          GPIO_PORTE_BASE
//#define DHT11_PIN           GPIO_PIN_3      // PE3
//
//#define LDR_PORT            GPIO_PORTE_BASE
//#define LDR_PIN             GPIO_PIN_2      // PE2 — AIN1
//
//#define FAN_PORT            GPIO_PORTC_BASE
//#define FAN_PIN             GPIO_PIN_4      // PC4
//
//#define BUZZER_PORT         GPIO_PORTC_BASE
//#define BUZZER_PIN          GPIO_PIN_5      // PC5
//
//#define PIR_PORT            GPIO_PORTD_BASE
//#define PIR_PIN             GPIO_PIN_0      // PD0
//
//#define WS2812_PORT         GPIO_PORTD_BASE
//#define WS2812_PIN          GPIO_PIN_1      // PD1 — LED strip data (330Ω in series)
//#define NUM_LEDS            16
//
//// Timing
////   SysCtlDelay(N) = 3N CPU cycles @ 16 MHz
////   1 us  ≈ SysCtlDelay(5)   (16 cycles / 3 ≈ 5)
////   18 ms ≈ SysCtlDelay(96000)
////   40 us ≈ SysCtlDelay(213)
////   WDT reload: 16e6 * 3 = 3-second timeout
//#define DHT11_START_DELAY   96000UL         // 18 ms pull-low start pulse
//#define DHT11_RELEASE_DELAY 160UL           // 30 us release to input
//#define DHT11_BIT_THRESHOLD 213UL           // 40 us sample threshold
//#define DHT11_MIN_PERIOD_MS 2000UL          // min ms between DHT11 reads
//#define DHT11_TIMEOUT       300UL           // loop iteration timeout per phase
//
//// WS2812B timing @ 16 MHz (SysCtlDelay = 3 cycles/iteration)
////   T1H = 800 ns  → 12.8 cycles → SysCtlDelay(4)   high time for bit-1
////   T1L = 450 ns  → 7.2 cycles  → SysCtlDelay(2)   low  time for bit-1
////   T0H = 400 ns  → 6.4 cycles  → SysCtlDelay(2)   high time for bit-0
////   T0L = 850 ns  → 13.6 cycles → SysCtlDelay(4)   low  time for bit-0
////   RES = >50 us  → handled by reset delay after frame
//#define WS_T1H   4UL
//#define WS_T1L   2UL
//#define WS_T0H   2UL
//#define WS_T0L   4UL
//
//#define WDT_RELOAD_CYCLES   (16000000UL * 3)
//#define POLL_INTERVAL_MS    5000UL          // sensor auto-poll period
//
//// Defaults
//#define FAN_THRESH_DEFAULT  80              // F — fan turns on above this
//#define LDR_DARK_THRESH     70              // % brightness — below = dark room
//#define MOTION_COOLDOWN_MS  5000UL          // min ms between motion events
//// Motion-triggered LED state machine
//// Sequence on first trigger: FLASH1_ON → FLASH1_OFF → FLASH2_ON → FLASH2_OFF → STEADY → DIM → IDLE
//// Re-trigger while STEADY or DIM: silently reset to STEADY at full brightness, no flash
//#define LED_FLASH_MS        150UL           // each flash ON/OFF phase duration
//#define LED_DIM_AFTER_MS    25000UL         // dim starts 25 s after flash sequence ends
//#define LED_OFF_AFTER_MS    30000UL         // strip turns off 30 s after flash sequence ends
//#define LED_DIM_BRIGHT      40              // brightness % during dim warning phase
//
//#define LED_MSTATE_IDLE       0
//#define LED_MSTATE_FLASH1_ON  1
//#define LED_MSTATE_FLASH1_OFF 2
//#define LED_MSTATE_FLASH2_ON  3
//#define LED_MSTATE_FLASH2_OFF 4
//#define LED_MSTATE_STEADY     5
//#define LED_MSTATE_DIM        6
//
//// =============================================================================
//// Global state
//// =============================================================================
//
//// UART ring buffer (filled by ISR)
//volatile char     rx_buf[RX_BUF_SIZE];
//volatile uint16_t rx_head = 0;
//volatile uint16_t rx_tail = 0;
//
//// Millisecond tick (incremented by SysTick ISR)
//volatile uint32_t g_tick_ms = 0;
//
//// Connection state
//static bool g_connected = true;
//
//// RGB state (mirrors physical pin state)
//static uint8_t g_r = 0, g_g = 0, g_b = 0;
//
//// DHT11 cached readings
//static int16_t  g_dht_temp_f    = 0;       // last valid temp in whole °F
//static uint8_t  g_dht_humid     = 0;       // last valid humidity %
//static bool     g_dht_valid     = false;   // true once first successful read
//static uint32_t g_dht_last_ms   = 0;       // tick of last successful read
//
//// Fan state
//static bool    g_fan_on     = false;
//static bool    g_fan_auto   = true;
//static uint8_t g_fan_thresh = FAN_THRESH_DEFAULT;
//
//// Motion state
//static bool          g_motion_armed = true;
//static volatile bool g_motion_evt   = false;
//
//// Buzzer state
//static volatile uint32_t g_buzz_until_ms = 0;
//
//// LDR / lighting state
//static bool    g_ldr_auto   = true;
//
//// WS2812B LED strip — GRB order, one [G,R,B] entry per LED
//// g_led_bright scales 0–100 and is applied at write time
//static uint8_t  g_led_buf[NUM_LEDS][3];    // [G, R, B] per LED
//static uint8_t  g_led_bright    = 100;     // global brightness %
//static bool     g_led_on        = false;   // is strip currently on
//// Motion LED state machine state and phase timestamps
//static uint8_t  g_led_motion_state  = LED_MSTATE_IDLE;
//static uint32_t g_led_phase_end_ms  = 0;  // end of current flash phase
//static uint32_t g_led_dim_ms        = 0;  // when dim warning begins
//static uint32_t g_led_off_ms        = 0;  // when strip turns off
//
//// Periodic poll tracking
//static uint32_t g_last_poll_ms   = 0;
//static uint32_t g_motion_last_ms = 0;
//
//// Reset cause
//static uint32_t g_reset_cause = 0;
//
//// Forward declarations
//void UART1_Handler(void);
//void GPIOD_Handler(void);
//void SysTick_Handler(void);
//void WDT0_Handler(void);
//
//// =============================================================================
//// UART output helpers
//// =============================================================================
//
//static void uart1_put_u32(uint32_t v)
//{
//    char tmp[11];
//    int  i = 0;
//
//    if (v == 0) {
//        UARTCharPut(UART1_BASE, '0');
//        return;
//    }
//    while (v > 0 && i < 10) {
//        tmp[i++] = (char)('0' + (v % 10));
//        v /= 10;
//    }
//    while (i--)
//        UARTCharPut(UART1_BASE, tmp[i]);
//}
//
//static void uart1_puts(const char *s)
//{
//    while (*s)
//        UARTCharPut(UART1_BASE, *s++);
//}
//
//static char bit_char(uint8_t v) { return v ? '1' : '0'; }
//
//static uint8_t parse_u8(const char *s)
//{
//    uint16_t v = 0;
//    while (*s >= '0' && *s <= '9') {
//        v = v * 10 + (uint16_t)(*s++ - '0');
//        if (v > 255) return 255;
//    }
//    return (uint8_t)v;
//}
//
//// Write a signed integer over UART (used for °F values)
//static void uart1_put_i16(int16_t v)
//{
//    if (v < 0) {
//        UARTCharPut(UART1_BASE, '-');
//        uart1_put_u32((uint32_t)(-v));
//    } else {
//        uart1_put_u32((uint32_t)v);
//    }
//}
//
//// =============================================================================
//// RGB LED
//// =============================================================================
//
//static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
//{
//    g_r = r ? 1 : 0;
//    g_g = g ? 1 : 0;
//    g_b = b ? 1 : 0;
//
//    uint8_t val = 0;
//    if (g_r) val |= GPIO_PIN_1; // Red   — PF1
//    if (g_b) val |= GPIO_PIN_2; // Blue  — PF2
//    if (g_g) val |= GPIO_PIN_3; // Green — PF3
//
//    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, val);
//}
//
//// =============================================================================
//// DHT11 — bit-bang 1-wire protocol on PE3
//// =============================================================================
//// Protocol summary:
////   1. MCU pulls data line LOW for 18 ms (start signal)
////   2. MCU releases line (external 10kΩ pulls HIGH)
////   3. DHT11 responds: 80 us LOW, 80 us HIGH
////   4. 40 data bits follow, each bit = 50 us LOW + (26-28 us=0 | 70 us=1) HIGH
////   5. 5 bytes: humidity_int, humidity_dec, temp_int, temp_dec, checksum
////
//// Interrupts are disabled for the duration of the read (~5 ms) to prevent
//// SysTick from corrupting the microsecond-level timing.
//// =============================================================================
//
//static void dht11_pin_output(void)
//{
//    GPIOPinTypeGPIOOutput(DHT11_PORT, DHT11_PIN);
//}
//
//static void dht11_pin_input(void)
//{
//    GPIOPinTypeGPIOInput(DHT11_PORT, DHT11_PIN);
//    // Rely on external 10kΩ pull-up — do not enable internal pull-up
//    GPIOPadConfigSet(DHT11_PORT, DHT11_PIN,
//                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
//}
//
//static uint8_t dht11_pin_read(void)
//{
//    return (GPIOPinRead(DHT11_PORT, DHT11_PIN) & DHT11_PIN) ? 1 : 0;
//}
//
//// Returns 0 on success, -1 on timeout or checksum failure.
//// On success, updates g_dht_temp_f, g_dht_humid, g_dht_valid, g_dht_last_ms.
//static int dht11_read(void)
//{
//    uint8_t  data[5] = {0};
//    uint32_t count;
//    int      bit;
//
//    // ---- Send start signal -----------------------------------------------
//    IntMasterDisable();         // disable all interrupts for timing accuracy
//
//    dht11_pin_output();
//    GPIOPinWrite(DHT11_PORT, DHT11_PIN, 0);     // pull LOW
//    SysCtlDelay(DHT11_START_DELAY);             // hold 18 ms
//
//    GPIOPinWrite(DHT11_PORT, DHT11_PIN, DHT11_PIN); // release HIGH
//    SysCtlDelay(DHT11_RELEASE_DELAY);           // wait 30 us
//
//    dht11_pin_input();                          // switch to input
//
//    // ---- Wait for DHT11 response LOW ------------------------------------
//    count = 0;
//    while (dht11_pin_read()) {
//        SysCtlDelay(5);                         // ~1 us per iteration
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Wait through DHT11's 80 us LOW ---------------------------------
//    count = 0;
//    while (!dht11_pin_read()) {
//        SysCtlDelay(5);
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Wait through DHT11's 80 us HIGH --------------------------------
//    count = 0;
//    while (dht11_pin_read()) {
//        SysCtlDelay(5);
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Read 40 data bits ----------------------------------------------
//    // Each bit: 50 us LOW, then 26-28 us HIGH (0) or 70 us HIGH (1)
//    // Strategy: wait for HIGH, wait 40 us, sample. HIGH=1, LOW=0.
//    for (bit = 0; bit < 40; bit++) {
//        // Wait for line to go HIGH (end of 50 us low period)
//        count = 0;
//        while (!dht11_pin_read()) {
//            SysCtlDelay(5);
//            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//        }
//
//        // Wait 40 us — threshold between 0-bit (~27 us high) and 1-bit (~70 us high)
//        SysCtlDelay(DHT11_BIT_THRESHOLD);
//
//        // Sample and shift into data byte
//        data[bit / 8] <<= 1;
//        if (dht11_pin_read())
//            data[bit / 8] |= 1;
//
//        // Wait for line to go LOW before next bit's 50 us low period
//        count = 0;
//        while (dht11_pin_read()) {
//            SysCtlDelay(5);
//            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//        }
//    }
//
//    IntMasterEnable();          // re-enable interrupts
//
//    // ---- Verify checksum ------------------------------------------------
//    // checksum = (hum_int + hum_dec + tmp_int + tmp_dec) & 0xFF
//    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
//        return -1;
//
//    // ---- Update cached state --------------------------------------------
//    // data[0] = humidity integer part (%), data[2] = temperature integer (C)
//    // DHT11 decimal bytes (data[1], data[3]) are always 0
//    uint8_t temp_c  = data[2];
//    uint8_t humid   = data[0];
//
//    g_dht_temp_f  = (int16_t)((temp_c * 9) / 5 + 32);  // C to F (integer)
//    g_dht_humid   = humid;
//    g_dht_valid   = true;
//    g_dht_last_ms = g_tick_ms;
//
//    return 0;
//}
//
//// Reads fresh data if >2 s have elapsed since last read, otherwise returns
//// cached values. Returns false if no valid data is available.
//static bool dht11_read_cached(void)
//{
//    if (!g_dht_valid || (g_tick_ms - g_dht_last_ms >= DHT11_MIN_PERIOD_MS))
//        dht11_read();      // attempt fresh read; on failure g_dht_valid stays false
//    return g_dht_valid;
//}
//
//// =============================================================================
//// ADC — KY-018 LDR only (AIN1 / PE2), sequencer 3 single-sample
//// =============================================================================
//
//static void adc0_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)) {}
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
//
//    // PE2 = AIN1 (LDR).  PE3 is DHT11 GPIO — do NOT configure as ADC.
//    GPIOPinTypeADC(LDR_PORT, LDR_PIN);
//
//    // Sequencer 3: single-sample, processor-triggered, highest priority
//    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
//    ADCSequenceStepConfigure(ADC0_BASE, 3, 0,
//                             ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
//    ADCSequenceEnable(ADC0_BASE, 3);
//    ADCIntClear(ADC0_BASE, 3);
//}
//
//// Returns raw 12-bit LDR sample
//static uint32_t adc0_read_ldr(void)
//{
//    uint32_t result = 0;
//    ADCProcessorTrigger(ADC0_BASE, 3);
//    while (!ADCIntStatus(ADC0_BASE, 3, false)) {}
//    ADCIntClear(ADC0_BASE, 3);
//    ADCSequenceDataGet(ADC0_BASE, 3, &result);
//    return result;
//}
//
//// KY-018: bright room -> low raw -> high brightness %
//// Returns 0 (dark) to 100 (bright)
//static uint8_t adc_to_light_pct(uint32_t raw)
//{
//    uint32_t pct = 100UL - (raw * 100UL / 4095UL);
//    return (uint8_t)(pct > 100 ? 100 : pct);
//}
//
//// =============================================================================
//// WS2812B LED strip — bit-bang on PD1 via 330Ω series resistor
//// =============================================================================
//// Protocol: 800 kHz, GRB bit order, MSB first.
//// Each bit is a fixed-period waveform:
////   '1' → HIGH for ~800 ns, LOW for ~450 ns
////   '0' → HIGH for ~400 ns, LOW for ~850 ns
//// After the full frame, hold LOW >50 us to latch (reset).
//// Interrupts are disabled during the entire frame write to prevent
//// SysTick from corrupting bit timing (~30 us per LED, ~480 us for 16 LEDs).
//// =============================================================================
//
//static void ws2812_init(void)
//{
//    // PD0 is PIR (already enabled GPIOD in pir_init).
//    // PD1 is the LED data line, direct connection, idle LOW.
//    GPIOPinTypeGPIOOutput(WS2812_PORT, WS2812_PIN);
//    GPIOPinWrite(WS2812_PORT, WS2812_PIN, 0);
//}
//
//// Scale a byte value by brightness percentage (0–100)
//static uint8_t ws_scale(uint8_t val, uint8_t bright)
//{
//    return (uint8_t)((uint16_t)val * bright / 100);
//}
//
//// Write the entire g_led_buf frame to the strip.
//// Uses inline NOPs instead of SysCtlDelay — SysCtlDelay call overhead
//// (~8 cycles) is too large for the short pulses needed at 16 MHz.
//// Each NOP = 1 cycle = 62.5 ns.
////
//// TI CCS assembler requires a leading space before each mnemonic in
//// inline asm strings — without it, NOP is parsed as a label definition.
////
//// Target timings (WS2812B spec ±150 ns tolerance):
////   T1H = 800 ns → ~750 ns  (1 write + 11 NOPs = 12 cycles)
////   T1L = 450 ns → ~375 ns  (1 write + 6 cycles loop overhead = 7 cycles)
////   T0H = 400 ns → ~312 ns  (1 write + 4 NOPs = 5 cycles)
////   T0L = 850 ns → ~937 ns  (1 write + 8 NOPs + 6 loop overhead = 15 cycles)
//static void ws2812_show(void)
//{
//    volatile uint32_t *gpio_data =
//        (volatile uint32_t *)(WS2812_PORT + 0x000 + (WS2812_PIN << 2));
//
//    uint8_t pin = WS2812_PIN;
//    int     led, byte, bit;
//
//    IntMasterDisable();
//
//    for (led = 0; led < NUM_LEDS; led++) {
//        for (byte = 0; byte < 3; byte++) {
//            uint8_t val = ws_scale(g_led_buf[led][byte], g_led_bright);
//            for (bit = 7; bit >= 0; bit--) {
//                if (val & (1 << bit)) {
//                    // Bit 1: ~750 ns HIGH, ~375 ns LOW
//                    *gpio_data = pin;
//                    __asm(" NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP");
//                    *gpio_data = 0;
//                    // T1L covered by loop overhead (~6 cycles)
//                } else {
//                    // Bit 0: ~312 ns HIGH, ~937 ns LOW
//                    *gpio_data = pin;
//                    __asm(" NOP\n NOP\n NOP\n NOP");
//                    *gpio_data = 0;
//                    __asm(" NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP");
//                    // T0L extended by loop overhead (~6 more cycles)
//                }
//            }
//        }
//    }
//
//    // Reset pulse: hold LOW >50 us to latch frame
//    *gpio_data = 0;
//    SysCtlDelay(300);
//
//    IntMasterEnable();
//}
//
//// Set all LEDs to one GRB color and push to strip
//static void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
//{
//    int i;
//    for (i = 0; i < NUM_LEDS; i++) {
//        g_led_buf[i][0] = g;   // WS2812B is GRB order
//        g_led_buf[i][1] = r;
//        g_led_buf[i][2] = b;
//    }
//    ws2812_show();
//}
//
//// Clear all LEDs (off)
//static void ws2812_clear(void)
//{
//    int i;
//    for (i = 0; i < NUM_LEDS; i++) {
//        g_led_buf[i][0] = 0;
//        g_led_buf[i][1] = 0;
//        g_led_buf[i][2] = 0;
//    }
//    ws2812_show();
//}
//
//// Turn strip on to white at current brightness
//static void led_on(void)
//{
//    ws2812_fill(255, 255, 255);
//    g_led_on = true;
//}
//
//// Turn strip off and reset the entire motion state machine
//static void led_off(void)
//{
//    ws2812_clear();
//    g_led_on           = false;
//    g_led_motion_state = LED_MSTATE_IDLE;
//    g_led_phase_end_ms = 0;
//    g_led_dim_ms       = 0;
//    g_led_off_ms       = 0;
//}
//
//// =============================================================================
//// PIR motion sensor — PD0, rising-edge interrupt
//// =============================================================================
//
//static void pir_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}
//
//    GPIOPinTypeGPIOInput(PIR_PORT, PIR_PIN);
//    GPIOPadConfigSet(PIR_PORT, PIR_PIN,
//                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
//    GPIOIntTypeSet(PIR_PORT, PIR_PIN, GPIO_RISING_EDGE);
//    GPIOIntRegister(PIR_PORT, GPIOD_Handler);
//    GPIOIntEnable(PIR_PORT, PIR_PIN);
//}
//
//void GPIOD_Handler(void)
//{
//    uint32_t status = GPIOIntStatus(PIR_PORT, true);
//    GPIOIntClear(PIR_PORT, status);
//    if ((status & PIR_PIN) && g_motion_armed)
//        g_motion_evt = true;
//}
//
//// =============================================================================
//// Active buzzer — PC5, GPIO HIGH = on
//// =============================================================================
//
//static void buzzer_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
//    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
//    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
//}
//
//static void buzzer_set(bool on)
//{
//    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, on ? BUZZER_PIN : 0);
//}
//
//// =============================================================================
//// Fan — PC4, GPIO HIGH = on  [STUB: transistor not yet wired]
//// =============================================================================
//
//static void fan_init(void)
//{
//    // GPIOC already enabled by buzzer_init
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
//    GPIOPinTypeGPIOOutput(FAN_PORT, FAN_PIN);
//    GPIOPinWrite(FAN_PORT, FAN_PIN, 0);
//}
//
//static void fan_set(bool on)
//{
//    g_fan_on = on;
//    GPIOPinWrite(FAN_PORT, FAN_PIN, on ? FAN_PIN : 0);
//}
//
//// =============================================================================
//// Hardware Watchdog — WDT0, 3-second timeout → reset
//// =============================================================================
//
//static void watchdog_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0)) {}
//    WatchdogReloadSet(WATCHDOG0_BASE, WDT_RELOAD_CYCLES);
//    WatchdogIntRegister(WATCHDOG0_BASE, WDT0_Handler);
//    WatchdogResetEnable(WATCHDOG0_BASE);
//    WatchdogEnable(WATCHDOG0_BASE);
//}
//
//void WDT0_Handler(void)
//{
//    // Intentionally empty — second timeout triggers hardware reset
//}
//
//static void watchdog_pet(void)
//{
//    WatchdogIntClear(WATCHDOG0_BASE);
//}
//
//// =============================================================================
//// SysTick — 1 ms tick counter
//// =============================================================================
//
//void SysTick_Handler(void)
//{
//    g_tick_ms++;
//}
//
//static void systick_init(void)
//{
//    SysTickPeriodSet(SysCtlClockGet() / 1000);  // 16000 cycles = 1 ms @ 16 MHz
//    SysTickIntRegister(SysTick_Handler);
//    SysTickIntEnable();
//    SysTickEnable();
//}
//
//// =============================================================================
//// UART1 — HC-05 Bluetooth, 9600 8N1, interrupt-driven RX
//// =============================================================================
//
//static void uart1_init_9600(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART1)) {}
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}
//
//    GPIOPinConfigure(GPIO_PB0_U1RX);
//    GPIOPinConfigure(GPIO_PB1_U1TX);
//    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
//
//    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 9600,
//                        UART_CONFIG_WLEN_8  |
//                        UART_CONFIG_STOP_ONE |
//                        UART_CONFIG_PAR_NONE);
//    UARTEnable(UART1_BASE);
//
//    UARTIntRegister(UART1_BASE, UART1_Handler);
//    UARTIntEnable(UART1_BASE, UART_INT_RX | UART_INT_RT);
//}
//
//void UART1_Handler(void)
//{
//    uint32_t status = UARTIntStatus(UART1_BASE, true);
//    UARTIntClear(UART1_BASE, status);
//    while (UARTCharsAvail(UART1_BASE)) {
//        char     c    = (char)UARTCharGetNonBlocking(UART1_BASE);
//        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
//        if (next != rx_tail) {
//            rx_buf[rx_head] = c;
//            rx_head = next;
//        }
//    }
//}
//
//static int read_line_uart1(char *buf, int max_len)
//{
//    static int i = 0;
//    while (rx_tail != rx_head) {
//        char c = rx_buf[rx_tail];
//        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
//        if (c == '\r' || c == '\n') {
//            if (i > 0) { buf[i] = '\0'; i = 0; return 1; }
//            continue;
//        }
//        if (i < max_len - 1)
//            buf[i++] = c;
//    }
//    return 0;
//}
//
//// =============================================================================
//// Periodic auto sensor logic — runs every POLL_INTERVAL_MS from main loop
//// =============================================================================
//
//static void run_auto_logic(void)
//{
//    // --- DHT11: refresh cached temp + humidity ---------------------------
//    dht11_read_cached();
//
//    // --- Fan auto control (uses integer °F from DHT11) -------------------
//    if (g_fan_auto && g_dht_valid) {
//        int16_t temp     = g_dht_temp_f;
//        int16_t thresh   = (int16_t)g_fan_thresh;
//        int16_t hyst     = thresh - 2;         // 2°F hysteresis band
//
//        if (!g_fan_on && temp >= thresh) {
//            fan_set(true);
//            uart1_puts("EVT FAN_ON TEMP=");
//            uart1_put_i16(g_dht_temp_f);
//            uart1_puts("F\r\n");
//        } else if (g_fan_on && temp < hyst) {
//            fan_set(false);
//            uart1_puts("EVT FAN_OFF\r\n");
//        }
//    }
//
//    // --- LDR auto-dim — controls strip brightness based on ambient light ----
//    // Dark room: strip on, brightness scales inversely with ambient level.
//    // Bright room: strip off (unless motion timer is active).
//    // Manual mode: user controls strip via GUI, nothing automatic here.
//    if (g_ldr_auto) {
//        uint8_t ambient = adc_to_light_pct(adc0_read_ldr());
//        if (ambient < LDR_DARK_THRESH) {
//            // Darker room = higher strip brightness (inverse scale)
//            uint8_t new_bright = (uint8_t)(100 - ambient);
//            g_led_bright = new_bright;
//            if (!g_led_on)
//                led_on();
//            else
//                ws2812_show();   // re-push same buffer at new brightness
//        } else {
//            // Bright room — turn strip off unless motion timer is running
//            if (g_led_on && g_led_motion_state == LED_MSTATE_IDLE) {
//                g_led_bright = 100;  // reset for next time
//                led_off();
//            }
//        }
//    }
//}
//
//// =============================================================================
//// Command parser
//// =============================================================================
//
//static void handle_cmd(const char *cmd)
//{
//    // --- Diagnostics -------------------------------------------------------
//
//    if (!strcmp(cmd, "PING")) {
//        uart1_puts("OK PONG\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "VERSION")) {
//        uart1_puts("OK FW 0.5\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "UPTIME")) {
//        uart1_puts("OK UPTIME=");
//        uart1_put_u32(g_tick_ms / 1000);
//        uart1_puts("s\r\n");
//        return;
//    }
//
//    // --- Full state snapshot -----------------------------------------------
//    // Format: OK RGB=xyz TEMP=XXF HUMID=XX% FAN=0 MODE=AUTO THRESH=80
//    //                MOTION=ARMED LIGHT=63 LDR=AUTO
//
//    if (!strcmp(cmd, "STATE")) {
//        dht11_read_cached();
//
//        uart1_puts("OK RGB=");
//        UARTCharPut(UART1_BASE, bit_char(g_r));
//        UARTCharPut(UART1_BASE, bit_char(g_g));
//        UARTCharPut(UART1_BASE, bit_char(g_b));
//
//        uart1_puts(" TEMP=");
//        if (g_dht_valid) {
//            uart1_put_i16(g_dht_temp_f);
//            UARTCharPut(UART1_BASE, 'F');
//        } else {
//            uart1_puts("ERR");
//        }
//
//        uart1_puts(" HUMID=");
//        if (g_dht_valid) {
//            uart1_put_u32(g_dht_humid);
//            UARTCharPut(UART1_BASE, '%');
//        } else {
//            uart1_puts("ERR");
//        }
//
//        uart1_puts(" FAN=");
//        UARTCharPut(UART1_BASE, g_fan_on ? '1' : '0');
//
//        uart1_puts(" MODE=");
//        uart1_puts(g_fan_auto ? "AUTO" : "MANUAL");
//
//        uart1_puts(" THRESH=");
//        uart1_put_u32(g_fan_thresh);
//
//        uart1_puts(" MOTION=");
//        uart1_puts(g_motion_armed ? "ARMED" : "DISARMED");
//
//        uart1_puts(" LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//
//        uart1_puts(" LDR=");
//        uart1_puts(g_ldr_auto ? "AUTO" : "MANUAL");
//
//        uart1_puts(" LED=");
//        UARTCharPut(UART1_BASE, g_led_on ? '1' : '0');
//
//        uart1_puts(" BRIGHT=");
//        uart1_put_u32(g_led_bright);
//
//        uart1_puts("\r\n");
//        return;
//    }
//
//    // --- Temperature -------------------------------------------------------
//
//    if (!strcmp(cmd, "TEMP")) {
//        dht11_read_cached();
//        uart1_puts("OK TEMP=");
//        if (g_dht_valid) {
//            uart1_put_i16(g_dht_temp_f);
//            uart1_puts("F\r\n");
//        } else {
//            uart1_puts("ERR\r\n");
//        }
//        return;
//    }
//
//    // --- Humidity ----------------------------------------------------------
//
//    if (!strcmp(cmd, "HUMID")) {
//        dht11_read_cached();
//        uart1_puts("OK HUMID=");
//        if (g_dht_valid) {
//            uart1_put_u32(g_dht_humid);
//            uart1_puts("%\r\n");
//        } else {
//            uart1_puts("ERR\r\n");
//        }
//        return;
//    }
//
//    // --- Light level -------------------------------------------------------
//
//    if (!strcmp(cmd, "LIGHT")) {
//        uart1_puts("OK LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//        return;
//    }
//
//    // --- Fan control -------------------------------------------------------
//
//    if (!strcmp(cmd, "FAN1")) {
//        g_fan_auto = false;
//        fan_set(true);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "FAN0")) {
//        g_fan_auto = false;
//        fan_set(false);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "FAN_AUTO")) {
//        g_fan_auto = true;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (strncmp(cmd, "FANTHRESH:", 10) == 0) {
//        uint8_t t = parse_u8(cmd + 10);
//        if (t >= 60 && t <= 100) {
//            g_fan_thresh = t;
//            uart1_puts("OK THRESH=");
//            uart1_put_u32(g_fan_thresh);
//            uart1_puts("\r\n");
//        } else {
//            uart1_puts("ERR RANGE 60-100\r\n");
//        }
//        return;
//    }
//
//    // --- Motion sensor -----------------------------------------------------
//
//    if (!strcmp(cmd, "MOTION_ARM")) {
//        g_motion_armed = true;
//        uart1_puts("OK MOTION=ARMED\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "MOTION_DISARM")) {
//        g_motion_armed = false;
//        uart1_puts("OK MOTION=DISARMED\r\n");
//        return;
//    }
//
//    // --- Buzzer ------------------------------------------------------------
//
//    if (strncmp(cmd, "BUZZ:", 5) == 0) {
//        uint8_t dur = parse_u8(cmd + 5);
//        if (dur > 0)
//            g_buzz_until_ms = g_tick_ms + (uint32_t)dur * 100;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "BUZZ0")) {
//        g_buzz_until_ms = 0;
//        buzzer_set(false);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- LDR / lighting ----------------------------------------------------
//
//    if (!strcmp(cmd, "LDR_AUTO")) {
//        g_ldr_auto = true;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // LDR_MAN:XX — manual brightness 0-100, disables auto-dim
//    if (strncmp(cmd, "LDR_MAN:", 8) == 0) {
//        uint8_t pct = parse_u8(cmd + 8);
//        if (pct > 100) pct = 100;
//        g_ldr_auto   = false;
//        g_led_bright = pct;
//        if (g_led_on)
//            ws2812_show();   // re-push at new brightness
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- WS2812B LED strip -------------------------------------------------
//
//    // LED_ON — turn strip on to white at current brightness
//    if (!strcmp(cmd, "LED_ON")) {
//        g_led_motion_state = LED_MSTATE_IDLE;  // cancel motion sequence
//        g_led_off_ms = 0;
//        led_on();
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // LED_OFF — turn strip off
//    if (!strcmp(cmd, "LED_OFF")) {
//        led_off();
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // LED_BRIGHT:XX — set brightness 0-100 and re-push if strip is on
//    if (strncmp(cmd, "LED_BRIGHT:", 11) == 0) {
//        uint8_t pct = parse_u8(cmd + 11);
//        if (pct > 100) pct = 100;
//        g_led_bright = pct;
//        if (g_led_on)
//            ws2812_show();
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- RGB LED -----------------------------------------------------------
//
//    if (cmd[0] == 'X' && cmd[1] == '\0') {
//        set_rgb(0, 0, 0);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (cmd[0] == 'R' && cmd[1] == 'G' && cmd[2] == 'B' && cmd[3] == ':' &&
//        (cmd[4] == '0' || cmd[4] == '1') &&
//        (cmd[5] == '0' || cmd[5] == '1') &&
//        (cmd[6] == '0' || cmd[6] == '1') &&
//        cmd[7] == '\0')
//    {
//        set_rgb(cmd[4] == '1', cmd[5] == '1', cmd[6] == '1');
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if ((cmd[0] == 'R' || cmd[0] == 'G' || cmd[0] == 'B') &&
//        (cmd[1] == '0' || cmd[1] == '1') &&
//        cmd[2] == '\0')
//    {
//        uint8_t on = (cmd[1] == '1');
//        if (cmd[0] == 'R') set_rgb(on,  g_g, g_b);
//        if (cmd[0] == 'G') set_rgb(g_r, on,  g_b);
//        if (cmd[0] == 'B') set_rgb(g_r, g_g, on );
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- Session -----------------------------------------------------------
//
//    if (!strcmp(cmd, "HELP")) {
//        uart1_puts("OK PING VERSION UPTIME STATE TEMP HUMID LIGHT "
//                   "FAN0 FAN1 FAN_AUTO FANTHRESH:XX "
//                   "MOTION_ARM MOTION_DISARM "
//                   "BUZZ:X BUZZ0 "
//                   "LDR_AUTO LDR_MAN:XX "
//                   "LED_ON LED_OFF LED_BRIGHT:XX "
//                   "RGB:xyz R0/R1 G0/G1 B0/B1 X EXIT\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "EXIT")) {
//        uart1_puts("OK DISCONNECTED\r\n");
//        g_connected = false;
//        return;
//    }
//
//    uart1_puts("ERR\r\n");
//}
//
//// =============================================================================
//// main
//// =============================================================================
//
//int main(void)
//{
//    // --- Clock: 16 MHz main oscillator ------------------------------------
//    SysCtlClockSet(SYSCTL_SYSDIV_1  |
//                   SYSCTL_USE_OSC   |
//                   SYSCTL_OSC_MAIN  |
//                   SYSCTL_XTAL_16MHZ);
//
//    g_reset_cause = SysCtlResetCauseGet();
//    SysCtlResetCauseClear(g_reset_cause);
//
//    // --- Peripheral init --------------------------------------------------
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
//    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE,
//                          GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
//    set_rgb(0, 0, 0);
//
//    // GPIOE: ADC needs PE2 (LDR), DHT11 needs PE3 — both enabled in adc0_init
//    adc0_init();
//
//    // DHT11 starts as input with external pull-up holding line HIGH (idle state)
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
//    dht11_pin_input();
//
//    uart1_init_9600();
//    pir_init();
//    buzzer_init();
//    fan_init();
//    ws2812_init();      // must come after pir_init (shares GPIOD)
//    systick_init();
//
//    IntMasterEnable();
//
//    // Clear strip on boot — ensures all LEDs start off
//    ws2812_clear();
//
//    // --- HC-05 boot delay (blue LED = waiting) ----------------------------
//    set_rgb(0, 0, 1);
//    SysCtlDelay(16000000UL * 2 / 3);   // ~2 s @ 16 MHz
//    set_rgb(0, 0, 0);
//
//    // --- Watchdog armed after boot delay ----------------------------------
//    watchdog_init();
//
//    // --- Startup message --------------------------------------------------
//    if (g_reset_cause & SYSCTL_CAUSE_WDOG0)
//        uart1_puts("EVT WDT_RESET\r\n");
//
//    uart1_puts("SYSTEM STATUS: READY\r\n"
//               "FW 0.5 | DHT11 + WS2812B | ENTER A COMMAND OR 'HELP'\r\n");
//
//    // Initial sensor read — sent to host immediately on connect
//    // DHT11 needs a moment after power-on before first read is reliable
//    SysCtlDelay(16000000UL / 3);       // 1 s additional DHT11 settle time
//    if (dht11_read() == 0) {
//        uart1_puts("STARTUP TEMP=");
//        uart1_put_i16(g_dht_temp_f);
//        uart1_puts("F HUMID=");
//        uart1_put_u32(g_dht_humid);
//        uart1_puts("% LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//    } else {
//        uart1_puts("STARTUP TEMP=ERR HUMID=ERR LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//    }
//
//    // --- Main loop --------------------------------------------------------
//    char line[CMD_BUF_SIZE];
//
//    while (1)
//    {
//        watchdog_pet();
//
//        // ── Motion event ─────────────────────────────────────────────────────
//        // PIR ISR sets g_motion_evt; 5-second cooldown prevents bounce spam.
//        //
//        // First trigger:  double-flash (2× 150 ms on/off) → steady white 25 s
//        //                 → dim to 40% for final 5 s → strip off. Total: ~30 s.
//        // Re-trigger:     if strip is already steady or dimming, silently reset
//        //                 the 30-second timer and restore full brightness.
//        //                 No flash — avoids annoying flicker for someone
//        //                 already in the room.
//        if (g_motion_evt) {
//            g_motion_evt = false;
//            if (g_tick_ms - g_motion_last_ms >= MOTION_COOLDOWN_MS) {
//                g_motion_last_ms = g_tick_ms;
//                uart1_puts("EVT MOTION\r\n");
//                g_buzz_until_ms = g_tick_ms + 300;
//
//                if (g_led_motion_state >= LED_MSTATE_STEADY) {
//                    // Re-trigger: restore full brightness, reset timer, no flash
//                    g_led_bright = 100;
//                    ws2812_show();
//                    g_led_dim_ms = g_tick_ms + LED_DIM_AFTER_MS;
//                    g_led_off_ms = g_tick_ms + LED_OFF_AFTER_MS;
//                    g_led_motion_state = LED_MSTATE_STEADY;
//                } else {
//                    // First trigger: start double-flash sequence
//                    g_led_bright = 100;
//                    led_on();
//                    g_led_phase_end_ms = g_tick_ms + LED_FLASH_MS;
//                    g_led_dim_ms       = g_tick_ms + (4 * LED_FLASH_MS) + LED_DIM_AFTER_MS;
//                    g_led_off_ms       = g_tick_ms + (4 * LED_FLASH_MS) + LED_OFF_AFTER_MS;
//                    g_led_motion_state = LED_MSTATE_FLASH1_ON;
//                }
//            }
//        }
//
//        // ── Motion LED state machine ──────────────────────────────────────────
//        switch (g_led_motion_state) {
//            case LED_MSTATE_FLASH1_ON:
//                if (g_tick_ms >= g_led_phase_end_ms) {
//                    ws2812_clear();
//                    g_led_on           = false;
//                    g_led_motion_state = LED_MSTATE_FLASH1_OFF;
//                    g_led_phase_end_ms += LED_FLASH_MS;
//                }
//                break;
//            case LED_MSTATE_FLASH1_OFF:
//                if (g_tick_ms >= g_led_phase_end_ms) {
//                    g_led_bright = 100;
//                    led_on();
//                    g_led_motion_state = LED_MSTATE_FLASH2_ON;
//                    g_led_phase_end_ms += LED_FLASH_MS;
//                }
//                break;
//            case LED_MSTATE_FLASH2_ON:
//                if (g_tick_ms >= g_led_phase_end_ms) {
//                    ws2812_clear();
//                    g_led_on           = false;
//                    g_led_motion_state = LED_MSTATE_FLASH2_OFF;
//                    g_led_phase_end_ms += LED_FLASH_MS;
//                }
//                break;
//            case LED_MSTATE_FLASH2_OFF:
//                if (g_tick_ms >= g_led_phase_end_ms) {
//                    // Flash complete — hold steady white
//                    g_led_bright = 100;
//                    led_on();
//                    g_led_motion_state = LED_MSTATE_STEADY;
//                }
//                break;
//            case LED_MSTATE_STEADY:
//                if (g_tick_ms >= g_led_dim_ms) {
//                    // 25 s elapsed — dim to 40% as "turning off soon" warning
//                    g_led_bright = LED_DIM_BRIGHT;
//                    ws2812_show();
//                    g_led_motion_state = LED_MSTATE_DIM;
//                }
//                break;
//            case LED_MSTATE_DIM:
//                if (g_tick_ms >= g_led_off_ms) {
//                    // 30 s elapsed — turn off and notify host
//                    led_off();
//                    uart1_puts("EVT LED_OFF\r\n");
//                }
//                break;
//            default:
//                break;
//        }
//
//        // Buzzer timer
//        if (g_buzz_until_ms > 0) {
//            if (g_tick_ms < g_buzz_until_ms)
//                buzzer_set(true);
//            else {
//                buzzer_set(false);
//                g_buzz_until_ms = 0;
//            }
//        }
//
//        // Periodic auto logic
//        if (g_tick_ms - g_last_poll_ms >= POLL_INTERVAL_MS) {
//            g_last_poll_ms = g_tick_ms;
//            run_auto_logic();
//        }
//
//        // UART command handler
//        if (g_connected && read_line_uart1(line, sizeof(line)))
//            handle_cmd(line);
//    }
//}










//#include <stdint.h>
//#include <stdbool.h>
//#include <string.h>
//
//#include "tm4c123gh6pm.h"
//#include "hw_memmap.h"
//#include "pin_map.h"
//
//#include "sysctl.h"
//#include "gpio.h"
//#include "uart.h"
//#include "interrupt.h"
//#include "adc.h"
//#include "watchdog.h"
//#include "systick.h"
//
//// =============================================================================
//// Constants
//// =============================================================================
//#define RX_BUF_SIZE         256
//#define CMD_BUF_SIZE        32
//
//// Pin assignments
//#define DHT11_PORT          GPIO_PORTE_BASE
//#define DHT11_PIN           GPIO_PIN_3      // PE3
//
//#define LDR_PORT            GPIO_PORTE_BASE
//#define LDR_PIN             GPIO_PIN_2      // PE2 — AIN1
//
//#define FAN_PORT            GPIO_PORTC_BASE
//#define FAN_PIN             GPIO_PIN_4      // PC4
//
//#define BUZZER_PORT         GPIO_PORTC_BASE
//#define BUZZER_PIN          GPIO_PIN_5      // PC5
//
//#define PIR_PORT            GPIO_PORTD_BASE
//#define PIR_PIN             GPIO_PIN_0      // PD0
//
//// Timing
////   SysCtlDelay(N) = 3N CPU cycles @ 16 MHz
////   1 us  ≈ SysCtlDelay(5)   (16 cycles / 3 ≈ 5)
////   18 ms ≈ SysCtlDelay(96000)
////   40 us ≈ SysCtlDelay(213)
////   WDT reload: 16e6 * 3 = 3-second timeout
//#define DHT11_START_DELAY   96000UL         // 18 ms pull-low start pulse
//#define DHT11_RELEASE_DELAY 160UL           // 30 us release to input
//#define DHT11_BIT_THRESHOLD 213UL           // 40 us sample threshold
//#define DHT11_MIN_PERIOD_MS 2000UL          // min ms between DHT11 reads
//#define DHT11_TIMEOUT       300UL           // loop iteration timeout per phase
//
//#define WDT_RELOAD_CYCLES   (16000000UL * 3)
//#define POLL_INTERVAL_MS    5000UL          // sensor auto-poll period
//
//// Defaults
//#define FAN_THRESH_DEFAULT  80              // F — fan turns on above this
//#define LDR_DARK_THRESH     70              // % brightness — below = dark room
//#define MOTION_COOLDOWN_MS  5000UL          // min ms between motion events
//
//// =============================================================================
//// Global state
//// =============================================================================
//
//// UART ring buffer (filled by ISR)
//volatile char     rx_buf[RX_BUF_SIZE];
//volatile uint16_t rx_head = 0;
//volatile uint16_t rx_tail = 0;
//
//// Millisecond tick (incremented by SysTick ISR)
//volatile uint32_t g_tick_ms = 0;
//
//// Connection state
//static bool g_connected = true;
//
//// RGB state (mirrors physical pin state)
//static uint8_t g_r = 0, g_g = 0, g_b = 0;
//
//// DHT11 cached readings
//static int16_t  g_dht_temp_f    = 0;       // last valid temp in whole °F
//static uint8_t  g_dht_humid     = 0;       // last valid humidity %
//static bool     g_dht_valid     = false;   // true once first successful read
//static uint32_t g_dht_last_ms   = 0;       // tick of last successful read
//
//// Fan state
//static bool    g_fan_on     = false;
//static bool    g_fan_auto   = true;
//static uint8_t g_fan_thresh = FAN_THRESH_DEFAULT;
//
//// Motion state
//static bool          g_motion_armed = true;
//static volatile bool g_motion_evt   = false;
//
//// Buzzer state
//static volatile uint32_t g_buzz_until_ms = 0;
//
//// LDR / lighting state
//static bool    g_ldr_auto   = true;
//static uint8_t g_ldr_manual = 100;
//
//// Periodic poll tracking
//static uint32_t g_last_poll_ms   = 0;
//static uint32_t g_motion_last_ms = 0;
//
//// Reset cause
//static uint32_t g_reset_cause = 0;
//
//// Forward declarations
//void UART1_Handler(void);
//void GPIOD_Handler(void);
//void SysTick_Handler(void);
//void WDT0_Handler(void);
//
//// =============================================================================
//// UART output helpers
//// =============================================================================
//
//static void uart1_put_u32(uint32_t v)
//{
//    char tmp[11];
//    int  i = 0;
//
//    if (v == 0) {
//        UARTCharPut(UART1_BASE, '0');
//        return;
//    }
//    while (v > 0 && i < 10) {
//        tmp[i++] = (char)('0' + (v % 10));
//        v /= 10;
//    }
//    while (i--)
//        UARTCharPut(UART1_BASE, tmp[i]);
//}
//
//static void uart1_puts(const char *s)
//{
//    while (*s)
//        UARTCharPut(UART1_BASE, *s++);
//}
//
//static char bit_char(uint8_t v) { return v ? '1' : '0'; }
//
//static uint8_t parse_u8(const char *s)
//{
//    uint16_t v = 0;
//    while (*s >= '0' && *s <= '9') {
//        v = v * 10 + (uint16_t)(*s++ - '0');
//        if (v > 255) return 255;
//    }
//    return (uint8_t)v;
//}
//
//// Write a signed integer over UART (used for °F values)
//static void uart1_put_i16(int16_t v)
//{
//    if (v < 0) {
//        UARTCharPut(UART1_BASE, '-');
//        uart1_put_u32((uint32_t)(-v));
//    } else {
//        uart1_put_u32((uint32_t)v);
//    }
//}
//
//// =============================================================================
//// RGB LED
//// =============================================================================
//
//static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
//{
//    g_r = r ? 1 : 0;
//    g_g = g ? 1 : 0;
//    g_b = b ? 1 : 0;
//
//    uint8_t val = 0;
//    if (g_r) val |= GPIO_PIN_1; // Red   — PF1
//    if (g_b) val |= GPIO_PIN_2; // Blue  — PF2
//    if (g_g) val |= GPIO_PIN_3; // Green — PF3
//
//    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, val);
//}
//
//// =============================================================================
//// DHT11 — bit-bang 1-wire protocol on PE3
//// =============================================================================
//// Protocol summary:
////   1. MCU pulls data line LOW for 18 ms (start signal)
////   2. MCU releases line (external 10kΩ pulls HIGH)
////   3. DHT11 responds: 80 us LOW, 80 us HIGH
////   4. 40 data bits follow, each bit = 50 us LOW + (26-28 us=0 | 70 us=1) HIGH
////   5. 5 bytes: humidity_int, humidity_dec, temp_int, temp_dec, checksum
////
//// Interrupts are disabled for the duration of the read (~5 ms) to prevent
//// SysTick from corrupting the microsecond-level timing.
//// =============================================================================
//
//static void dht11_pin_output(void)
//{
//    GPIOPinTypeGPIOOutput(DHT11_PORT, DHT11_PIN);
//}
//
//static void dht11_pin_input(void)
//{
//    GPIOPinTypeGPIOInput(DHT11_PORT, DHT11_PIN);
//    // Rely on external 10kΩ pull-up — do not enable internal pull-up
//    GPIOPadConfigSet(DHT11_PORT, DHT11_PIN,
//                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
//}
//
//static uint8_t dht11_pin_read(void)
//{
//    return (GPIOPinRead(DHT11_PORT, DHT11_PIN) & DHT11_PIN) ? 1 : 0;
//}
//
//// Returns 0 on success, -1 on timeout or checksum failure.
//// On success, updates g_dht_temp_f, g_dht_humid, g_dht_valid, g_dht_last_ms.
//static int dht11_read(void)
//{
//    uint8_t  data[5] = {0};
//    uint32_t count;
//    int      bit;
//
//    // ---- Send start signal -----------------------------------------------
//    IntMasterDisable();         // disable all interrupts for timing accuracy
//
//    dht11_pin_output();
//    GPIOPinWrite(DHT11_PORT, DHT11_PIN, 0);     // pull LOW
//    SysCtlDelay(DHT11_START_DELAY);             // hold 18 ms
//
//    GPIOPinWrite(DHT11_PORT, DHT11_PIN, DHT11_PIN); // release HIGH
//    SysCtlDelay(DHT11_RELEASE_DELAY);           // wait 30 us
//
//    dht11_pin_input();                          // switch to input
//
//    // ---- Wait for DHT11 response LOW ------------------------------------
//    count = 0;
//    while (dht11_pin_read()) {
//        SysCtlDelay(5);                         // ~1 us per iteration
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Wait through DHT11's 80 us LOW ---------------------------------
//    count = 0;
//    while (!dht11_pin_read()) {
//        SysCtlDelay(5);
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Wait through DHT11's 80 us HIGH --------------------------------
//    count = 0;
//    while (dht11_pin_read()) {
//        SysCtlDelay(5);
//        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//    }
//
//    // ---- Read 40 data bits ----------------------------------------------
//    // Each bit: 50 us LOW, then 26-28 us HIGH (0) or 70 us HIGH (1)
//    // Strategy: wait for HIGH, wait 40 us, sample. HIGH=1, LOW=0.
//    for (bit = 0; bit < 40; bit++) {
//        // Wait for line to go HIGH (end of 50 us low period)
//        count = 0;
//        while (!dht11_pin_read()) {
//            SysCtlDelay(5);
//            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//        }
//
//        // Wait 40 us — threshold between 0-bit (~27 us high) and 1-bit (~70 us high)
//        SysCtlDelay(DHT11_BIT_THRESHOLD);
//
//        // Sample and shift into data byte
//        data[bit / 8] <<= 1;
//        if (dht11_pin_read())
//            data[bit / 8] |= 1;
//
//        // Wait for line to go LOW before next bit's 50 us low period
//        count = 0;
//        while (dht11_pin_read()) {
//            SysCtlDelay(5);
//            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
//        }
//    }
//
//    IntMasterEnable();          // re-enable interrupts
//
//    // ---- Verify checksum ------------------------------------------------
//    // checksum = (hum_int + hum_dec + tmp_int + tmp_dec) & 0xFF
//    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
//        return -1;
//
//    // ---- Update cached state --------------------------------------------
//    // data[0] = humidity integer part (%), data[2] = temperature integer (C)
//    // DHT11 decimal bytes (data[1], data[3]) are always 0
//    uint8_t temp_c  = data[2];
//    uint8_t humid   = data[0];
//
//    g_dht_temp_f  = (int16_t)((temp_c * 9) / 5 + 32);  // C to F (integer)
//    g_dht_humid   = humid;
//    g_dht_valid   = true;
//    g_dht_last_ms = g_tick_ms;
//
//    return 0;
//}
//
//// Reads fresh data if >2 s have elapsed since last read, otherwise returns
//// cached values. Returns false if no valid data is available.
//static bool dht11_read_cached(void)
//{
//    if (!g_dht_valid || (g_tick_ms - g_dht_last_ms >= DHT11_MIN_PERIOD_MS))
//        dht11_read();      // attempt fresh read; on failure g_dht_valid stays false
//    return g_dht_valid;
//}
//
//// =============================================================================
//// ADC — KY-018 LDR only (AIN1 / PE2), sequencer 3 single-sample
//// =============================================================================
//
//static void adc0_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)) {}
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
//
//    // PE2 = AIN1 (LDR).  PE3 is DHT11 GPIO — do NOT configure as ADC.
//    GPIOPinTypeADC(LDR_PORT, LDR_PIN);
//
//    // Sequencer 3: single-sample, processor-triggered, highest priority
//    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
//    ADCSequenceStepConfigure(ADC0_BASE, 3, 0,
//                             ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
//    ADCSequenceEnable(ADC0_BASE, 3);
//    ADCIntClear(ADC0_BASE, 3);
//}
//
//// Returns raw 12-bit LDR sample
//static uint32_t adc0_read_ldr(void)
//{
//    uint32_t result = 0;
//    ADCProcessorTrigger(ADC0_BASE, 3);
//    while (!ADCIntStatus(ADC0_BASE, 3, false)) {}
//    ADCIntClear(ADC0_BASE, 3);
//    ADCSequenceDataGet(ADC0_BASE, 3, &result);
//    return result;
//}
//
//// KY-018: bright room -> low raw -> high brightness %
//// Returns 0 (dark) to 100 (bright)
//static uint8_t adc_to_light_pct(uint32_t raw)
//{
//    uint32_t pct = 100UL - (raw * 100UL / 4095UL);
//    return (uint8_t)(pct > 100 ? 100 : pct);
//}
//
//// =============================================================================
//// PIR motion sensor — PD0, rising-edge interrupt
//// =============================================================================
//
//static void pir_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}
//
//    GPIOPinTypeGPIOInput(PIR_PORT, PIR_PIN);
//    GPIOPadConfigSet(PIR_PORT, PIR_PIN,
//                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
//    GPIOIntTypeSet(PIR_PORT, PIR_PIN, GPIO_RISING_EDGE);
//    GPIOIntRegister(PIR_PORT, GPIOD_Handler);
//    GPIOIntEnable(PIR_PORT, PIR_PIN);
//}
//
//void GPIOD_Handler(void)
//{
//    uint32_t status = GPIOIntStatus(PIR_PORT, true);
//    GPIOIntClear(PIR_PORT, status);
//    if ((status & PIR_PIN) && g_motion_armed)
//        g_motion_evt = true;
//}
//
//// =============================================================================
//// Active buzzer — PC5, GPIO HIGH = on
//// =============================================================================
//
//static void buzzer_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
//    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
//    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, 0);
//}
//
//static void buzzer_set(bool on)
//{
//    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, on ? BUZZER_PIN : 0);
//}
//
//// =============================================================================
//// Fan — PC4, GPIO HIGH = on  [STUB: transistor not yet wired]
//// =============================================================================
//
//static void fan_init(void)
//{
//    // GPIOC already enabled by buzzer_init
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
//    GPIOPinTypeGPIOOutput(FAN_PORT, FAN_PIN);
//    GPIOPinWrite(FAN_PORT, FAN_PIN, 0);
//}
//
//static void fan_set(bool on)
//{
//    g_fan_on = on;
//    GPIOPinWrite(FAN_PORT, FAN_PIN, on ? FAN_PIN : 0);
//}
//
//// =============================================================================
//// Hardware Watchdog — WDT0, 3-second timeout → reset
//// =============================================================================
//
//static void watchdog_init(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0)) {}
//    WatchdogReloadSet(WATCHDOG0_BASE, WDT_RELOAD_CYCLES);
//    WatchdogIntRegister(WATCHDOG0_BASE, WDT0_Handler);
//    WatchdogResetEnable(WATCHDOG0_BASE);
//    WatchdogEnable(WATCHDOG0_BASE);
//}
//
//void WDT0_Handler(void)
//{
//    // Intentionally empty — second timeout triggers hardware reset
//}
//
//static void watchdog_pet(void)
//{
//    WatchdogIntClear(WATCHDOG0_BASE);
//}
//
//// =============================================================================
//// SysTick — 1 ms tick counter
//// =============================================================================
//
//void SysTick_Handler(void)
//{
//    g_tick_ms++;
//}
//
//static void systick_init(void)
//{
//    SysTickPeriodSet(SysCtlClockGet() / 1000);  // 16000 cycles = 1 ms @ 16 MHz
//    SysTickIntRegister(SysTick_Handler);
//    SysTickIntEnable();
//    SysTickEnable();
//}
//
//// =============================================================================
//// UART1 — HC-05 Bluetooth, 9600 8N1, interrupt-driven RX
//// =============================================================================
//
//static void uart1_init_9600(void)
//{
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART1)) {}
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}
//
//    GPIOPinConfigure(GPIO_PB0_U1RX);
//    GPIOPinConfigure(GPIO_PB1_U1TX);
//    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);
//
//    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 9600,
//                        UART_CONFIG_WLEN_8  |
//                        UART_CONFIG_STOP_ONE |
//                        UART_CONFIG_PAR_NONE);
//    UARTEnable(UART1_BASE);
//
//    UARTIntRegister(UART1_BASE, UART1_Handler);
//    UARTIntEnable(UART1_BASE, UART_INT_RX | UART_INT_RT);
//}
//
//void UART1_Handler(void)
//{
//    uint32_t status = UARTIntStatus(UART1_BASE, true);
//    UARTIntClear(UART1_BASE, status);
//    while (UARTCharsAvail(UART1_BASE)) {
//        char     c    = (char)UARTCharGetNonBlocking(UART1_BASE);
//        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
//        if (next != rx_tail) {
//            rx_buf[rx_head] = c;
//            rx_head = next;
//        }
//    }
//}
//
//static int read_line_uart1(char *buf, int max_len)
//{
//    static int i = 0;
//    while (rx_tail != rx_head) {
//        char c = rx_buf[rx_tail];
//        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
//        if (c == '\r' || c == '\n') {
//            if (i > 0) { buf[i] = '\0'; i = 0; return 1; }
//            continue;
//        }
//        if (i < max_len - 1)
//            buf[i++] = c;
//    }
//    return 0;
//}
//
//// =============================================================================
//// Periodic auto sensor logic — runs every POLL_INTERVAL_MS from main loop
//// =============================================================================
//
//static void run_auto_logic(void)
//{
//    // --- DHT11: refresh cached temp + humidity ---------------------------
//    dht11_read_cached();
//
//    // --- Fan auto control (uses integer °F from DHT11) -------------------
//    if (g_fan_auto && g_dht_valid) {
//        int16_t temp     = g_dht_temp_f;
//        int16_t thresh   = (int16_t)g_fan_thresh;
//        int16_t hyst     = thresh - 2;         // 2°F hysteresis band
//
//        if (!g_fan_on && temp >= thresh) {
//            fan_set(true);
//            uart1_puts("EVT FAN_ON TEMP=");
//            uart1_put_i16(g_dht_temp_f);
//            uart1_puts("F\r\n");
//        } else if (g_fan_on && temp < hyst) {
//            fan_set(false);
//            uart1_puts("EVT FAN_OFF\r\n");
//        }
//    }
//
//    // --- LDR auto-dim (onboard blue LED placeholder) ---------------------
//    if (g_ldr_auto) {
//        uint8_t brightness = adc_to_light_pct(adc0_read_ldr());
//        if (brightness < LDR_DARK_THRESH)
//            set_rgb(g_r, g_g, 1);
//        else
//            set_rgb(g_r, g_g, 0);
//    }
//}
//
//// =============================================================================
//// Command parser
//// =============================================================================
//
//static void handle_cmd(const char *cmd)
//{
//    // --- Diagnostics -------------------------------------------------------
//
//    if (!strcmp(cmd, "PING")) {
//        uart1_puts("OK PONG\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "VERSION")) {
//        uart1_puts("OK FW 0.4\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "UPTIME")) {
//        uart1_puts("OK UPTIME=");
//        uart1_put_u32(g_tick_ms / 1000);
//        uart1_puts("s\r\n");
//        return;
//    }
//
//    // --- Full state snapshot -----------------------------------------------
//    // Format: OK RGB=xyz TEMP=XXF HUMID=XX% FAN=0 MODE=AUTO THRESH=80
//    //                MOTION=ARMED LIGHT=63 LDR=AUTO
//
//    if (!strcmp(cmd, "STATE")) {
//        dht11_read_cached();
//
//        uart1_puts("OK RGB=");
//        UARTCharPut(UART1_BASE, bit_char(g_r));
//        UARTCharPut(UART1_BASE, bit_char(g_g));
//        UARTCharPut(UART1_BASE, bit_char(g_b));
//
//        uart1_puts(" TEMP=");
//        if (g_dht_valid) {
//            uart1_put_i16(g_dht_temp_f);
//            UARTCharPut(UART1_BASE, 'F');
//        } else {
//            uart1_puts("ERR");
//        }
//
//        uart1_puts(" HUMID=");
//        if (g_dht_valid) {
//            uart1_put_u32(g_dht_humid);
//            UARTCharPut(UART1_BASE, '%');
//        } else {
//            uart1_puts("ERR");
//        }
//
//        uart1_puts(" FAN=");
//        UARTCharPut(UART1_BASE, g_fan_on ? '1' : '0');
//
//        uart1_puts(" MODE=");
//        uart1_puts(g_fan_auto ? "AUTO" : "MANUAL");
//
//        uart1_puts(" THRESH=");
//        uart1_put_u32(g_fan_thresh);
//
//        uart1_puts(" MOTION=");
//        uart1_puts(g_motion_armed ? "ARMED" : "DISARMED");
//
//        uart1_puts(" LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//
//        uart1_puts(" LDR=");
//        uart1_puts(g_ldr_auto ? "AUTO" : "MANUAL");
//
//        uart1_puts("\r\n");
//        return;
//    }
//
//    // --- Temperature -------------------------------------------------------
//
//    if (!strcmp(cmd, "TEMP")) {
//        dht11_read_cached();
//        uart1_puts("OK TEMP=");
//        if (g_dht_valid) {
//            uart1_put_i16(g_dht_temp_f);
//            uart1_puts("F\r\n");
//        } else {
//            uart1_puts("ERR\r\n");
//        }
//        return;
//    }
//
//    // --- Humidity ----------------------------------------------------------
//
//    if (!strcmp(cmd, "HUMID")) {
//        dht11_read_cached();
//        uart1_puts("OK HUMID=");
//        if (g_dht_valid) {
//            uart1_put_u32(g_dht_humid);
//            uart1_puts("%\r\n");
//        } else {
//            uart1_puts("ERR\r\n");
//        }
//        return;
//    }
//
//    // --- Light level -------------------------------------------------------
//
//    if (!strcmp(cmd, "LIGHT")) {
//        uart1_puts("OK LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//        return;
//    }
//
//    // --- Fan control -------------------------------------------------------
//
//    if (!strcmp(cmd, "FAN1")) {
//        g_fan_auto = false;
//        fan_set(true);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "FAN0")) {
//        g_fan_auto = false;
//        fan_set(false);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "FAN_AUTO")) {
//        g_fan_auto = true;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (strncmp(cmd, "FANTHRESH:", 10) == 0) {
//        uint8_t t = parse_u8(cmd + 10);
//        if (t >= 60 && t <= 100) {
//            g_fan_thresh = t;
//            uart1_puts("OK THRESH=");
//            uart1_put_u32(g_fan_thresh);
//            uart1_puts("\r\n");
//        } else {
//            uart1_puts("ERR RANGE 60-100\r\n");
//        }
//        return;
//    }
//
//    // --- Motion sensor -----------------------------------------------------
//
//    if (!strcmp(cmd, "MOTION_ARM")) {
//        g_motion_armed = true;
//        uart1_puts("OK MOTION=ARMED\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "MOTION_DISARM")) {
//        g_motion_armed = false;
//        uart1_puts("OK MOTION=DISARMED\r\n");
//        return;
//    }
//
//    // --- Buzzer ------------------------------------------------------------
//
//    if (strncmp(cmd, "BUZZ:", 5) == 0) {
//        uint8_t dur = parse_u8(cmd + 5);
//        if (dur > 0)
//            g_buzz_until_ms = g_tick_ms + (uint32_t)dur * 100;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "BUZZ0")) {
//        g_buzz_until_ms = 0;
//        buzzer_set(false);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- LDR / lighting ----------------------------------------------------
//
//    if (!strcmp(cmd, "LDR_AUTO")) {
//        g_ldr_auto = true;
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (strncmp(cmd, "LDR_MAN:", 8) == 0) {
//        uint8_t pct = parse_u8(cmd + 8);
//        if (pct > 100) pct = 100;
//        g_ldr_auto   = false;
//        g_ldr_manual = pct;
//        set_rgb(g_r, g_g, pct >= 50 ? 1 : 0);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- RGB LED -----------------------------------------------------------
//
//    if (cmd[0] == 'X' && cmd[1] == '\0') {
//        set_rgb(0, 0, 0);
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if (cmd[0] == 'R' && cmd[1] == 'G' && cmd[2] == 'B' && cmd[3] == ':' &&
//        (cmd[4] == '0' || cmd[4] == '1') &&
//        (cmd[5] == '0' || cmd[5] == '1') &&
//        (cmd[6] == '0' || cmd[6] == '1') &&
//        cmd[7] == '\0')
//    {
//        set_rgb(cmd[4] == '1', cmd[5] == '1', cmd[6] == '1');
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    if ((cmd[0] == 'R' || cmd[0] == 'G' || cmd[0] == 'B') &&
//        (cmd[1] == '0' || cmd[1] == '1') &&
//        cmd[2] == '\0')
//    {
//        uint8_t on = (cmd[1] == '1');
//        if (cmd[0] == 'R') set_rgb(on,  g_g, g_b);
//        if (cmd[0] == 'G') set_rgb(g_r, on,  g_b);
//        if (cmd[0] == 'B') set_rgb(g_r, g_g, on );
//        uart1_puts("OK\r\n");
//        return;
//    }
//
//    // --- Session -----------------------------------------------------------
//
//    if (!strcmp(cmd, "HELP")) {
//        uart1_puts("OK PING VERSION UPTIME STATE TEMP HUMID LIGHT "
//                   "FAN0 FAN1 FAN_AUTO FANTHRESH:XX "
//                   "MOTION_ARM MOTION_DISARM "
//                   "BUZZ:X BUZZ0 "
//                   "LDR_AUTO LDR_MAN:XX "
//                   "RGB:xyz R0/R1 G0/G1 B0/B1 X EXIT\r\n");
//        return;
//    }
//
//    if (!strcmp(cmd, "EXIT")) {
//        uart1_puts("OK DISCONNECTED\r\n");
//        g_connected = false;
//        return;
//    }
//
//    uart1_puts("ERR\r\n");
//}
//
//// =============================================================================
//// main
//// =============================================================================
//
//int main(void)
//{
//    // --- Clock: 16 MHz main oscillator ------------------------------------
//    SysCtlClockSet(SYSCTL_SYSDIV_1  |
//                   SYSCTL_USE_OSC   |
//                   SYSCTL_OSC_MAIN  |
//                   SYSCTL_XTAL_16MHZ);
//
//    g_reset_cause = SysCtlResetCauseGet();
//    SysCtlResetCauseClear(g_reset_cause);
//
//    // --- Peripheral init --------------------------------------------------
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
//    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE,
//                          GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
//    set_rgb(0, 0, 0);
//
//    // GPIOE: ADC needs PE2 (LDR), DHT11 needs PE3 — both enabled in adc0_init
//    adc0_init();
//
//    // DHT11 starts as input with external pull-up holding line HIGH (idle state)
//    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
//    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
//    dht11_pin_input();
//
//    uart1_init_9600();
//    pir_init();
//    buzzer_init();
//    fan_init();
//    systick_init();
//
//    IntMasterEnable();
//
//    // --- HC-05 boot delay (blue LED = waiting) ----------------------------
//    set_rgb(0, 0, 1);
//    SysCtlDelay(16000000UL * 2 / 3);   // ~2 s @ 16 MHz
//    set_rgb(0, 0, 0);
//
//    // --- Watchdog armed after boot delay ----------------------------------
//    watchdog_init();
//
//    // --- Startup message --------------------------------------------------
//    if (g_reset_cause & SYSCTL_CAUSE_WDOG0)
//        uart1_puts("EVT WDT_RESET\r\n");
//
//    uart1_puts("SYSTEM STATUS: READY\r\n"
//               "FW 0.4 | DHT11 | ENTER A COMMAND OR 'HELP'\r\n");
//
//    // Initial sensor read — sent to host immediately on connect
//    // DHT11 needs a moment after power-on before first read is reliable
//    SysCtlDelay(16000000UL / 3);       // 1 s additional DHT11 settle time
//    if (dht11_read() == 0) {
//        uart1_puts("STARTUP TEMP=");
//        uart1_put_i16(g_dht_temp_f);
//        uart1_puts("F HUMID=");
//        uart1_put_u32(g_dht_humid);
//        uart1_puts("% LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//    } else {
//        uart1_puts("STARTUP TEMP=ERR HUMID=ERR LIGHT=");
//        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
//        uart1_puts("\r\n");
//    }
//
//    // --- Main loop --------------------------------------------------------
//    char line[CMD_BUF_SIZE];
//
//    while (1)
//    {
//        watchdog_pet();
//
//        // Motion event
//        if (g_motion_evt) {
//            g_motion_evt = false;
//            if (g_tick_ms - g_motion_last_ms >= MOTION_COOLDOWN_MS) {
//                g_motion_last_ms = g_tick_ms;
//                uart1_puts("EVT MOTION\r\n");
//                g_buzz_until_ms = g_tick_ms + 300;
//            }
//        }
//
//        // Buzzer timer
//        if (g_buzz_until_ms > 0) {
//            if (g_tick_ms < g_buzz_until_ms)
//                buzzer_set(true);
//            else {
//                buzzer_set(false);
//                g_buzz_until_ms = 0;
//            }
//        }
//
//        // Periodic auto logic
//        if (g_tick_ms - g_last_poll_ms >= POLL_INTERVAL_MS) {
//            g_last_poll_ms = g_tick_ms;
//            run_auto_logic();
//        }
//
//        // UART command handler
//        if (g_connected && read_line_uart1(line, sizeof(line)))
//            handle_cmd(line);
//    }
//}







// =============================================================================
// TM4C123G Smart Home Controller — Firmware v0.4
// =============================================================================
// Peripherals:
//   DHT11   — PE3 (GPIO)  : temperature + humidity sensor (1-wire bit-bang)
//   KY-018  — PE2 (AIN1)  : photoresistor / ambient light (ADC sequencer 3)
//   HC-SR501— PD0         : PIR motion sensor (rising-edge interrupt)
//   Buzzer  — PC5         : active buzzer (GPIO HIGH = on)
//   Fan     — PC4         : DC fan via 2N2222A (GPIO HIGH = on) [STUB]
//   HC-05   — PB0/PB1     : Bluetooth UART1 (9600 8N1)
//   RGB LED — PF1/PF2/PF3 : onboard RGB LED (R/B/G)
//
// DHT11 wiring:
//   DATA → PE3  +  10kΩ pull-up between PE3 and 3.3V
//   VCC  → 3.3V
//   GND  → GND
// =============================================================================

#include <stdint.h>
#include <stdbool.h>
#include <string.h>

#include "tm4c123gh6pm.h"
#include "hw_memmap.h"
#include "pin_map.h"

#include "sysctl.h"
#include "gpio.h"
#include "uart.h"
#include "interrupt.h"
#include "adc.h"
#include "watchdog.h"
#include "systick.h"

// =============================================================================
// Constants
// =============================================================================
#define RX_BUF_SIZE         256
#define CMD_BUF_SIZE        32

// Pin assignments
#define DHT11_PORT          GPIO_PORTE_BASE
#define DHT11_PIN           GPIO_PIN_3      // PE3

#define LDR_PORT            GPIO_PORTE_BASE
#define LDR_PIN             GPIO_PIN_2      // PE2 — AIN1

#define FAN_PORT            GPIO_PORTC_BASE
#define FAN_PIN             GPIO_PIN_4      // PC4

#define BUZZER_PORT         GPIO_PORTC_BASE
#define BUZZER_PIN          GPIO_PIN_5      // PC5

#define PIR_PORT            GPIO_PORTD_BASE
#define PIR_PIN             GPIO_PIN_0      // PD0

#define WS2812_PORT         GPIO_PORTD_BASE
#define WS2812_PIN          GPIO_PIN_1      // PD1 — LED strip data (330Ω in series)
#define NUM_LEDS            16

// Timing
//   SysCtlDelay(N) = 3N CPU cycles @ 16 MHz
//   1 us  ≈ SysCtlDelay(5)   (16 cycles / 3 ≈ 5)
//   18 ms ≈ SysCtlDelay(96000)
//   40 us ≈ SysCtlDelay(213)
//   WDT reload: 16e6 * 3 = 3-second timeout
#define DHT11_START_DELAY   96000UL         // 18 ms pull-low start pulse
#define DHT11_RELEASE_DELAY 160UL           // 30 us release to input
#define DHT11_BIT_THRESHOLD 213UL           // 40 us sample threshold
#define DHT11_MIN_PERIOD_MS 2000UL          // min ms between DHT11 reads
#define DHT11_TIMEOUT       300UL           // loop iteration timeout per phase

// WS2812B timing @ 16 MHz (SysCtlDelay = 3 cycles/iteration)
//   T1H = 800 ns  → 12.8 cycles → SysCtlDelay(4)   high time for bit-1
//   T1L = 450 ns  → 7.2 cycles  → SysCtlDelay(2)   low  time for bit-1
//   T0H = 400 ns  → 6.4 cycles  → SysCtlDelay(2)   high time for bit-0
//   T0L = 850 ns  → 13.6 cycles → SysCtlDelay(4)   low  time for bit-0
//   RES = >50 us  → handled by reset delay after frame
#define WS_T1H   4UL
#define WS_T1L   2UL
#define WS_T0H   2UL
#define WS_T0L   4UL

#define WDT_RELOAD_CYCLES   (16000000UL * 3)
#define POLL_INTERVAL_MS    5000UL          // sensor auto-poll period

// Defaults
#define FAN_THRESH_DEFAULT  80              // F — fan turns on above this
#define LDR_DARK_THRESH     70              // % brightness — below = dark room
#define MOTION_COOLDOWN_MS  5000UL          // min ms between motion events
// Motion-triggered LED state machine
// Sequence on first trigger: FLASH1_ON → FLASH1_OFF → FLASH2_ON → FLASH2_OFF → STEADY → DIM → IDLE
// Re-trigger while STEADY or DIM: silently reset to STEADY at full brightness, no flash
#define LED_FLASH_MS        150UL           // each flash ON/OFF phase duration
#define LED_DIM_AFTER_MS    25000UL         // dim starts 25 s after flash sequence ends
#define LED_OFF_AFTER_MS    30000UL         // strip turns off 30 s after flash sequence ends
#define LED_DIM_BRIGHT      40              // brightness % during dim warning phase

#define LED_MSTATE_IDLE       0
#define LED_MSTATE_FLASH1_ON  1
#define LED_MSTATE_FLASH1_OFF 2
#define LED_MSTATE_FLASH2_ON  3
#define LED_MSTATE_FLASH2_OFF 4
#define LED_MSTATE_STEADY     5
#define LED_MSTATE_DIM        6

// =============================================================================
// Global state
// =============================================================================

// UART ring buffer (filled by ISR)
volatile char     rx_buf[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;

// Millisecond tick (incremented by SysTick ISR)
volatile uint32_t g_tick_ms = 0;

// Connection state
static bool g_connected = true;

// RGB state (mirrors physical pin state)
static uint8_t g_r = 0, g_g = 0, g_b = 0;

// DHT11 cached readings
static int16_t  g_dht_temp_f    = 0;       // last valid temp in whole °F
static uint8_t  g_dht_humid     = 0;       // last valid humidity %
static bool     g_dht_valid     = false;   // true once first successful read
static uint32_t g_dht_last_ms   = 0;       // tick of last successful read

// Fan state
static bool    g_fan_on     = false;
static bool    g_fan_auto   = true;
static uint8_t g_fan_thresh = FAN_THRESH_DEFAULT;

// Motion state
static bool          g_motion_armed = true;
static volatile bool g_motion_evt   = false;

// Buzzer state
static volatile uint32_t g_buzz_until_ms = 0;

// LDR / lighting state
static bool    g_ldr_auto   = true;

// WS2812B LED strip — GRB order, one [G,R,B] entry per LED
// g_led_bright scales 0–100 and is applied at write time
static uint8_t  g_led_buf[NUM_LEDS][3];    // [G, R, B] per LED
static uint8_t  g_led_bright    = 100;     // global brightness %
static bool     g_led_on        = false;   // is strip currently on
// Motion LED state machine state and phase timestamps
static uint8_t  g_led_motion_state  = LED_MSTATE_IDLE;
static uint32_t g_led_phase_end_ms  = 0;  // end of current flash phase
static uint32_t g_led_dim_ms        = 0;  // when dim warning begins
static uint32_t g_led_off_ms        = 0;  // when strip turns off

// Periodic poll tracking
static uint32_t g_last_poll_ms   = 0;
static uint32_t g_motion_last_ms = 0;

// Reset cause
static uint32_t g_reset_cause = 0;

// Forward declarations
void UART1_Handler(void);
void GPIOD_Handler(void);
void SysTick_Handler(void);
void WDT0_Handler(void);

// =============================================================================
// UART output helpers
// =============================================================================

static void uart1_put_u32(uint32_t v)
{
    char tmp[11];
    int  i = 0;

    if (v == 0) {
        UARTCharPut(UART1_BASE, '0');
        return;
    }
    while (v > 0 && i < 10) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }
    while (i--)
        UARTCharPut(UART1_BASE, tmp[i]);
}

static void uart1_puts(const char *s)
{
    while (*s)
        UARTCharPut(UART1_BASE, *s++);
}

static char bit_char(uint8_t v) { return v ? '1' : '0'; }

static uint8_t parse_u8(const char *s)
{
    uint16_t v = 0;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (uint16_t)(*s++ - '0');
        if (v > 255) return 255;
    }
    return (uint8_t)v;
}

// Write a signed integer over UART (used for °F values)
static void uart1_put_i16(int16_t v)
{
    if (v < 0) {
        UARTCharPut(UART1_BASE, '-');
        uart1_put_u32((uint32_t)(-v));
    } else {
        uart1_put_u32((uint32_t)v);
    }
}

// =============================================================================
// RGB LED
// =============================================================================

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    g_r = r ? 1 : 0;
    g_g = g ? 1 : 0;
    g_b = b ? 1 : 0;

    uint8_t val = 0;
    if (g_r) val |= GPIO_PIN_1; // Red   — PF1
    if (g_b) val |= GPIO_PIN_2; // Blue  — PF2
    if (g_g) val |= GPIO_PIN_3; // Green — PF3

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, val);
}

// =============================================================================
// DHT11 — bit-bang 1-wire protocol on PE3
// =============================================================================
// Protocol summary:
//   1. MCU pulls data line LOW for 18 ms (start signal)
//   2. MCU releases line (external 10kΩ pulls HIGH)
//   3. DHT11 responds: 80 us LOW, 80 us HIGH
//   4. 40 data bits follow, each bit = 50 us LOW + (26-28 us=0 | 70 us=1) HIGH
//   5. 5 bytes: humidity_int, humidity_dec, temp_int, temp_dec, checksum
//
// Interrupts are disabled for the duration of the read (~5 ms) to prevent
// SysTick from corrupting the microsecond-level timing.
// =============================================================================

static void dht11_pin_output(void)
{
    GPIOPinTypeGPIOOutput(DHT11_PORT, DHT11_PIN);
}

static void dht11_pin_input(void)
{
    GPIOPinTypeGPIOInput(DHT11_PORT, DHT11_PIN);
    // Rely on external 10kΩ pull-up — do not enable internal pull-up
    GPIOPadConfigSet(DHT11_PORT, DHT11_PIN,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
}

static uint8_t dht11_pin_read(void)
{
    return (GPIOPinRead(DHT11_PORT, DHT11_PIN) & DHT11_PIN) ? 1 : 0;
}

// Returns 0 on success, -1 on timeout or checksum failure.
// On success, updates g_dht_temp_f, g_dht_humid, g_dht_valid, g_dht_last_ms.
static int dht11_read(void)
{
    uint8_t  data[5] = {0};
    uint32_t count;
    int      bit;

    // ---- Send start signal -----------------------------------------------
    IntMasterDisable();         // disable all interrupts for timing accuracy

    dht11_pin_output();
    GPIOPinWrite(DHT11_PORT, DHT11_PIN, 0);     // pull LOW
    SysCtlDelay(DHT11_START_DELAY);             // hold 18 ms

    GPIOPinWrite(DHT11_PORT, DHT11_PIN, DHT11_PIN); // release HIGH
    SysCtlDelay(DHT11_RELEASE_DELAY);           // wait 30 us

    dht11_pin_input();                          // switch to input

    // ---- Wait for DHT11 response LOW ------------------------------------
    count = 0;
    while (dht11_pin_read()) {
        SysCtlDelay(5);                         // ~1 us per iteration
        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
    }

    // ---- Wait through DHT11's 80 us LOW ---------------------------------
    count = 0;
    while (!dht11_pin_read()) {
        SysCtlDelay(5);
        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
    }

    // ---- Wait through DHT11's 80 us HIGH --------------------------------
    count = 0;
    while (dht11_pin_read()) {
        SysCtlDelay(5);
        if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
    }

    // ---- Read 40 data bits ----------------------------------------------
    // Each bit: 50 us LOW, then 26-28 us HIGH (0) or 70 us HIGH (1)
    // Strategy: wait for HIGH, wait 40 us, sample. HIGH=1, LOW=0.
    for (bit = 0; bit < 40; bit++) {
        // Wait for line to go HIGH (end of 50 us low period)
        count = 0;
        while (!dht11_pin_read()) {
            SysCtlDelay(5);
            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
        }

        // Wait 40 us — threshold between 0-bit (~27 us high) and 1-bit (~70 us high)
        SysCtlDelay(DHT11_BIT_THRESHOLD);

        // Sample and shift into data byte
        data[bit / 8] <<= 1;
        if (dht11_pin_read())
            data[bit / 8] |= 1;

        // Wait for line to go LOW before next bit's 50 us low period
        count = 0;
        while (dht11_pin_read()) {
            SysCtlDelay(5);
            if (++count > DHT11_TIMEOUT) { IntMasterEnable(); return -1; }
        }
    }

    IntMasterEnable();          // re-enable interrupts

    // ---- Verify checksum ------------------------------------------------
    // checksum = (hum_int + hum_dec + tmp_int + tmp_dec) & 0xFF
    if (((data[0] + data[1] + data[2] + data[3]) & 0xFF) != data[4])
        return -1;

    // ---- Update cached state --------------------------------------------
    // data[0] = humidity integer part (%), data[2] = temperature integer (C)
    // DHT11 decimal bytes (data[1], data[3]) are always 0
    uint8_t temp_c  = data[2];
    uint8_t humid   = data[0];

    g_dht_temp_f  = (int16_t)((temp_c * 9) / 5 + 32);  // C to F (integer)
    g_dht_humid   = humid;
    g_dht_valid   = true;
    g_dht_last_ms = g_tick_ms;

    return 0;
}

// Reads fresh data if >2 s have elapsed since last read, otherwise returns
// cached values. Returns false if no valid data is available.
static bool dht11_read_cached(void)
{
    if (!g_dht_valid || (g_tick_ms - g_dht_last_ms >= DHT11_MIN_PERIOD_MS))
        dht11_read();      // attempt fresh read; on failure g_dht_valid stays false
    return g_dht_valid;
}

// =============================================================================
// ADC — KY-018 LDR only (AIN1 / PE2), sequencer 3 single-sample
// =============================================================================

static void adc0_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_ADC0);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_ADC0)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}

    // PE2 = AIN1 (LDR).  PE3 is DHT11 GPIO — do NOT configure as ADC.
    GPIOPinTypeADC(LDR_PORT, LDR_PIN);

    // Sequencer 3: single-sample, processor-triggered, highest priority
    ADCSequenceConfigure(ADC0_BASE, 3, ADC_TRIGGER_PROCESSOR, 0);
    ADCSequenceStepConfigure(ADC0_BASE, 3, 0,
                             ADC_CTL_CH1 | ADC_CTL_IE | ADC_CTL_END);
    ADCSequenceEnable(ADC0_BASE, 3);
    ADCIntClear(ADC0_BASE, 3);
}

// Returns raw 12-bit LDR sample
static uint32_t adc0_read_ldr(void)
{
    uint32_t result = 0;
    ADCProcessorTrigger(ADC0_BASE, 3);
    while (!ADCIntStatus(ADC0_BASE, 3, false)) {}
    ADCIntClear(ADC0_BASE, 3);
    ADCSequenceDataGet(ADC0_BASE, 3, &result);
    return result;
}

// KY-018: bright room -> low raw -> high brightness %
// Returns 0 (dark) to 100 (bright)
static uint8_t adc_to_light_pct(uint32_t raw)
{
    uint32_t pct = 100UL - (raw * 100UL / 4095UL);
    return (uint8_t)(pct > 100 ? 100 : pct);
}

// =============================================================================
// WS2812B LED strip — bit-bang on PD1 via 330Ω series resistor
// =============================================================================
// Protocol: 800 kHz, GRB bit order, MSB first.
// Each bit is a fixed-period waveform:
//   '1' → HIGH for ~800 ns, LOW for ~450 ns
//   '0' → HIGH for ~400 ns, LOW for ~850 ns
// After the full frame, hold LOW >50 us to latch (reset).
// Interrupts are disabled during the entire frame write to prevent
// SysTick from corrupting bit timing (~30 us per LED, ~480 us for 16 LEDs).
// =============================================================================

static void ws2812_init(void)
{
    // PD0 is PIR (already enabled GPIOD in pir_init).
    // PD1 is the LED data line, direct connection, idle LOW.
    GPIOPinTypeGPIOOutput(WS2812_PORT, WS2812_PIN);
    GPIOPinWrite(WS2812_PORT, WS2812_PIN, 0);
}

// Scale a byte value by brightness percentage (0–100)
static uint8_t ws_scale(uint8_t val, uint8_t bright)
{
    return (uint8_t)((uint16_t)val * bright / 100);
}

// Write the entire g_led_buf frame to the strip.
// Uses inline NOPs instead of SysCtlDelay — SysCtlDelay call overhead
// (~8 cycles) is too large for the short pulses needed at 16 MHz.
// Each NOP = 1 cycle = 62.5 ns.
//
// TI CCS assembler requires a leading space before each mnemonic in
// inline asm strings — without it, NOP is parsed as a label definition.
//
// Target timings (WS2812B spec ±150 ns tolerance):
//   T1H = 800 ns → ~750 ns  (1 write + 11 NOPs = 12 cycles)
//   T1L = 450 ns → ~375 ns  (1 write + 6 cycles loop overhead = 7 cycles)
//   T0H = 400 ns → ~312 ns  (1 write + 4 NOPs = 5 cycles)
//   T0L = 850 ns → ~937 ns  (1 write + 8 NOPs + 6 loop overhead = 15 cycles)
static void ws2812_show(void)
{
    volatile uint32_t *gpio_data =
        (volatile uint32_t *)(WS2812_PORT + 0x000 + (WS2812_PIN << 2));

    uint8_t pin = WS2812_PIN;
    int     led, byte, bit;

    IntMasterDisable();

    for (led = 0; led < NUM_LEDS; led++) {
        for (byte = 0; byte < 3; byte++) {
            uint8_t val = ws_scale(g_led_buf[led][byte], g_led_bright);
            for (bit = 7; bit >= 0; bit--) {
                if (val & (1 << bit)) {
                    // Bit 1: ~750 ns HIGH, ~375 ns LOW
                    *gpio_data = pin;
                    __asm(" NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP");
                    *gpio_data = 0;
                    // T1L covered by loop overhead (~6 cycles)
                } else {
                    // Bit 0: ~312 ns HIGH, ~937 ns LOW
                    *gpio_data = pin;
                    __asm(" NOP\n NOP\n NOP\n NOP");
                    *gpio_data = 0;
                    __asm(" NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP\n NOP");
                    // T0L extended by loop overhead (~6 more cycles)
                }
            }
        }
    }

    // Reset pulse: hold LOW >50 us to latch frame
    *gpio_data = 0;
    SysCtlDelay(300);

    IntMasterEnable();
}

// Set all LEDs to one GRB color and push to strip
static void ws2812_fill(uint8_t r, uint8_t g, uint8_t b)
{
    int i;
    for (i = 0; i < NUM_LEDS; i++) {
        g_led_buf[i][0] = g;   // WS2812B is GRB order
        g_led_buf[i][1] = r;
        g_led_buf[i][2] = b;
    }
    ws2812_show();
}

// Clear all LEDs (off)
static void ws2812_clear(void)
{
    int i;
    for (i = 0; i < NUM_LEDS; i++) {
        g_led_buf[i][0] = 0;
        g_led_buf[i][1] = 0;
        g_led_buf[i][2] = 0;
    }
    ws2812_show();
}

// Turn strip on to white at current brightness
static void led_on(void)
{
    ws2812_fill(255, 255, 255);
    g_led_on = true;
}

// Turn strip off and reset the entire motion state machine
static void led_off(void)
{
    ws2812_clear();
    g_led_on           = false;
    g_led_motion_state = LED_MSTATE_IDLE;
    g_led_phase_end_ms = 0;
    g_led_dim_ms       = 0;
    g_led_off_ms       = 0;
}

// =============================================================================
// PIR motion sensor — PD0, rising-edge interrupt
// =============================================================================

static void pir_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOD);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOD)) {}

    GPIOPinTypeGPIOInput(PIR_PORT, PIR_PIN);
    GPIOPadConfigSet(PIR_PORT, PIR_PIN,
                     GPIO_STRENGTH_2MA, GPIO_PIN_TYPE_STD);
    GPIOIntTypeSet(PIR_PORT, PIR_PIN, GPIO_RISING_EDGE);
    GPIOIntRegister(PIR_PORT, GPIOD_Handler);
    GPIOIntEnable(PIR_PORT, PIR_PIN);
}

void GPIOD_Handler(void)
{
    uint32_t status = GPIOIntStatus(PIR_PORT, true);
    GPIOIntClear(PIR_PORT, status);
    if ((status & PIR_PIN) && g_motion_armed)
        g_motion_evt = true;
}

// =============================================================================
// Active buzzer — PC5, GPIO HIGH = on
// =============================================================================

static void buzzer_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
    GPIOPinTypeGPIOOutput(BUZZER_PORT, BUZZER_PIN);
    // Active buzzer module uses inverted logic: LOW = on, HIGH = off.
    // Set HIGH at init so buzzer is silent at boot.
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, BUZZER_PIN);
}

static void buzzer_set(bool on)
{
    // Inverted logic: drive LOW to buzz, HIGH to silence
    GPIOPinWrite(BUZZER_PORT, BUZZER_PIN, on ? 0 : BUZZER_PIN);
}

// =============================================================================
// Fan — PC4, GPIO HIGH = on  [STUB: transistor not yet wired]
// =============================================================================

static void fan_init(void)
{
    // GPIOC already enabled by buzzer_init
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOC);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOC)) {}
    GPIOPinTypeGPIOOutput(FAN_PORT, FAN_PIN);
    GPIOPinWrite(FAN_PORT, FAN_PIN, 0);
}

static void fan_set(bool on)
{
    g_fan_on = on;
    GPIOPinWrite(FAN_PORT, FAN_PIN, on ? FAN_PIN : 0);
}

// =============================================================================
// Hardware Watchdog — WDT0, 3-second timeout → reset
// =============================================================================

static void watchdog_init(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_WDOG0);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_WDOG0)) {}
    WatchdogReloadSet(WATCHDOG0_BASE, WDT_RELOAD_CYCLES);
    WatchdogIntRegister(WATCHDOG0_BASE, WDT0_Handler);
    WatchdogResetEnable(WATCHDOG0_BASE);
    WatchdogEnable(WATCHDOG0_BASE);
}

void WDT0_Handler(void)
{
    // Intentionally empty — second timeout triggers hardware reset
}

static void watchdog_pet(void)
{
    WatchdogIntClear(WATCHDOG0_BASE);
}

// =============================================================================
// SysTick — 1 ms tick counter
// =============================================================================

void SysTick_Handler(void)
{
    g_tick_ms++;
}

static void systick_init(void)
{
    SysTickPeriodSet(SysCtlClockGet() / 1000);  // 16000 cycles = 1 ms @ 16 MHz
    SysTickIntRegister(SysTick_Handler);
    SysTickIntEnable();
    SysTickEnable();
}

// =============================================================================
// UART1 — HC-05 Bluetooth, 9600 8N1, interrupt-driven RX
// =============================================================================

static void uart1_init_9600(void)
{
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_UART1)) {}
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}

    GPIOPinConfigure(GPIO_PB0_U1RX);
    GPIOPinConfigure(GPIO_PB1_U1TX);
    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 9600,
                        UART_CONFIG_WLEN_8  |
                        UART_CONFIG_STOP_ONE |
                        UART_CONFIG_PAR_NONE);
    UARTEnable(UART1_BASE);

    UARTIntRegister(UART1_BASE, UART1_Handler);
    UARTIntEnable(UART1_BASE, UART_INT_RX | UART_INT_RT);
}

void UART1_Handler(void)
{
    uint32_t status = UARTIntStatus(UART1_BASE, true);
    UARTIntClear(UART1_BASE, status);
    while (UARTCharsAvail(UART1_BASE)) {
        char     c    = (char)UARTCharGetNonBlocking(UART1_BASE);
        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;
        if (next != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

static int read_line_uart1(char *buf, int max_len)
{
    static int i = 0;
    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;
        if (c == '\r' || c == '\n') {
            if (i > 0) { buf[i] = '\0'; i = 0; return 1; }
            continue;
        }
        if (i < max_len - 1)
            buf[i++] = c;
    }
    return 0;
}

// =============================================================================
// Periodic auto sensor logic — runs every POLL_INTERVAL_MS from main loop
// =============================================================================

static void run_auto_logic(void)
{
    // --- DHT11: refresh cached temp + humidity ---------------------------
    dht11_read_cached();

    // --- Fan auto control (uses integer °F from DHT11) -------------------
    if (g_fan_auto && g_dht_valid) {
        int16_t temp     = g_dht_temp_f;
        int16_t thresh   = (int16_t)g_fan_thresh;
        int16_t hyst     = thresh - 2;         // 2°F hysteresis band

        if (!g_fan_on && temp >= thresh) {
            fan_set(true);
            uart1_puts("EVT FAN_ON TEMP=");
            uart1_put_i16(g_dht_temp_f);
            uart1_puts("F\r\n");
        } else if (g_fan_on && temp < hyst) {
            fan_set(false);
            uart1_puts("EVT FAN_OFF\r\n");
        }
    }

    // --- LDR auto-dim — controls strip brightness based on ambient light ----
    // Dark room: strip on, brightness scales inversely with ambient level.
    // Bright room: strip off (unless motion timer is active).
    // Manual mode: user controls strip via GUI, nothing automatic here.
    if (g_ldr_auto) {
        uint8_t ambient = adc_to_light_pct(adc0_read_ldr());
        if (ambient < LDR_DARK_THRESH) {
            // Darker room = higher strip brightness (inverse scale)
            uint8_t new_bright = (uint8_t)(100 - ambient);
            g_led_bright = new_bright;
            if (!g_led_on)
                led_on();
            else
                ws2812_show();   // re-push same buffer at new brightness
        } else {
            // Bright room — turn strip off unless motion timer is running
            if (g_led_on && g_led_motion_state == LED_MSTATE_IDLE) {
                g_led_bright = 100;  // reset for next time
                led_off();
            }
        }
    }
}

// =============================================================================
// Command parser
// =============================================================================

static void handle_cmd(const char *cmd)
{
    // --- Diagnostics -------------------------------------------------------

    if (!strcmp(cmd, "PING")) {
        uart1_puts("OK PONG\r\n");
        return;
    }

    if (!strcmp(cmd, "VERSION")) {
        uart1_puts("OK FW 0.5\r\n");
        return;
    }

    if (!strcmp(cmd, "UPTIME")) {
        uart1_puts("OK UPTIME=");
        uart1_put_u32(g_tick_ms / 1000);
        uart1_puts("s\r\n");
        return;
    }

    // --- Full state snapshot -----------------------------------------------
    // Format: OK RGB=xyz TEMP=XXF HUMID=XX% FAN=0 MODE=AUTO THRESH=80
    //                MOTION=ARMED LIGHT=63 LDR=AUTO

    if (!strcmp(cmd, "STATE")) {
        dht11_read_cached();

        uart1_puts("OK RGB=");
        UARTCharPut(UART1_BASE, bit_char(g_r));
        UARTCharPut(UART1_BASE, bit_char(g_g));
        UARTCharPut(UART1_BASE, bit_char(g_b));

        uart1_puts(" TEMP=");
        if (g_dht_valid) {
            uart1_put_i16(g_dht_temp_f);
            UARTCharPut(UART1_BASE, 'F');
        } else {
            uart1_puts("ERR");
        }

        uart1_puts(" HUMID=");
        if (g_dht_valid) {
            uart1_put_u32(g_dht_humid);
            UARTCharPut(UART1_BASE, '%');
        } else {
            uart1_puts("ERR");
        }

        uart1_puts(" FAN=");
        UARTCharPut(UART1_BASE, g_fan_on ? '1' : '0');

        uart1_puts(" MODE=");
        uart1_puts(g_fan_auto ? "AUTO" : "MANUAL");

        uart1_puts(" THRESH=");
        uart1_put_u32(g_fan_thresh);

        uart1_puts(" MOTION=");
        uart1_puts(g_motion_armed ? "ARMED" : "DISARMED");

        uart1_puts(" LIGHT=");
        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));

        uart1_puts(" LDR=");
        uart1_puts(g_ldr_auto ? "AUTO" : "MANUAL");

        uart1_puts(" LED=");
        UARTCharPut(UART1_BASE, g_led_on ? '1' : '0');

        uart1_puts(" BRIGHT=");
        uart1_put_u32(g_led_bright);

        uart1_puts("\r\n");
        return;
    }

    // --- Temperature -------------------------------------------------------

    if (!strcmp(cmd, "TEMP")) {
        dht11_read_cached();
        uart1_puts("OK TEMP=");
        if (g_dht_valid) {
            uart1_put_i16(g_dht_temp_f);
            uart1_puts("F\r\n");
        } else {
            uart1_puts("ERR\r\n");
        }
        return;
    }

    // --- Humidity ----------------------------------------------------------

    if (!strcmp(cmd, "HUMID")) {
        dht11_read_cached();
        uart1_puts("OK HUMID=");
        if (g_dht_valid) {
            uart1_put_u32(g_dht_humid);
            uart1_puts("%\r\n");
        } else {
            uart1_puts("ERR\r\n");
        }
        return;
    }

    // --- Light level -------------------------------------------------------

    if (!strcmp(cmd, "LIGHT")) {
        uart1_puts("OK LIGHT=");
        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
        uart1_puts("\r\n");
        return;
    }

    // --- Fan control -------------------------------------------------------

    if (!strcmp(cmd, "FAN1")) {
        g_fan_auto = false;
        fan_set(true);
        uart1_puts("OK\r\n");
        return;
    }

    if (!strcmp(cmd, "FAN0")) {
        g_fan_auto = false;
        fan_set(false);
        uart1_puts("OK\r\n");
        return;
    }

    if (!strcmp(cmd, "FAN_AUTO")) {
        g_fan_auto = true;
        uart1_puts("OK\r\n");
        return;
    }

    if (strncmp(cmd, "FANTHRESH:", 10) == 0) {
        uint8_t t = parse_u8(cmd + 10);
        if (t >= 60 && t <= 100) {
            g_fan_thresh = t;
            uart1_puts("OK THRESH=");
            uart1_put_u32(g_fan_thresh);
            uart1_puts("\r\n");
        } else {
            uart1_puts("ERR RANGE 60-100\r\n");
        }
        return;
    }

    // --- Motion sensor -----------------------------------------------------

    if (!strcmp(cmd, "MOTION_ARM")) {
        g_motion_armed = true;
        uart1_puts("OK MOTION=ARMED\r\n");
        return;
    }

    if (!strcmp(cmd, "MOTION_DISARM")) {
        g_motion_armed = false;
        uart1_puts("OK MOTION=DISARMED\r\n");
        return;
    }

    // --- Buzzer ------------------------------------------------------------

    if (strncmp(cmd, "BUZZ:", 5) == 0) {
        uint8_t dur = parse_u8(cmd + 5);
        if (dur > 0)
            g_buzz_until_ms = g_tick_ms + (uint32_t)dur * 100;
        uart1_puts("OK\r\n");
        return;
    }

    if (!strcmp(cmd, "BUZZ0")) {
        g_buzz_until_ms = 0;
        buzzer_set(false);
        uart1_puts("OK\r\n");
        return;
    }

    // --- LDR / lighting ----------------------------------------------------

    if (!strcmp(cmd, "LDR_AUTO")) {
        g_ldr_auto = true;
        uart1_puts("OK\r\n");
        return;
    }

    // LDR_MAN:XX — manual brightness 0-100, disables auto-dim
    if (strncmp(cmd, "LDR_MAN:", 8) == 0) {
        uint8_t pct = parse_u8(cmd + 8);
        if (pct > 100) pct = 100;
        g_ldr_auto   = false;
        g_led_bright = pct;
        if (g_led_on)
            ws2812_show();   // re-push at new brightness
        uart1_puts("OK\r\n");
        return;
    }

    // --- WS2812B LED strip -------------------------------------------------

    // LED_ON — turn strip on to white at current brightness
    if (!strcmp(cmd, "LED_ON")) {
        g_led_motion_state = LED_MSTATE_IDLE;  // cancel motion sequence
        g_led_off_ms = 0;
        led_on();
        uart1_puts("OK\r\n");
        return;
    }

    // LED_OFF — turn strip off
    if (!strcmp(cmd, "LED_OFF")) {
        led_off();
        uart1_puts("OK\r\n");
        return;
    }

    // LED_BRIGHT:XX — set brightness 0-100 and re-push if strip is on
    if (strncmp(cmd, "LED_BRIGHT:", 11) == 0) {
        uint8_t pct = parse_u8(cmd + 11);
        if (pct > 100) pct = 100;
        g_led_bright = pct;
        if (g_led_on)
            ws2812_show();
        uart1_puts("OK\r\n");
        return;
    }

    // --- RGB LED -----------------------------------------------------------

    if (cmd[0] == 'X' && cmd[1] == '\0') {
        set_rgb(0, 0, 0);
        uart1_puts("OK\r\n");
        return;
    }

    if (cmd[0] == 'R' && cmd[1] == 'G' && cmd[2] == 'B' && cmd[3] == ':' &&
        (cmd[4] == '0' || cmd[4] == '1') &&
        (cmd[5] == '0' || cmd[5] == '1') &&
        (cmd[6] == '0' || cmd[6] == '1') &&
        cmd[7] == '\0')
    {
        set_rgb(cmd[4] == '1', cmd[5] == '1', cmd[6] == '1');
        uart1_puts("OK\r\n");
        return;
    }

    if ((cmd[0] == 'R' || cmd[0] == 'G' || cmd[0] == 'B') &&
        (cmd[1] == '0' || cmd[1] == '1') &&
        cmd[2] == '\0')
    {
        uint8_t on = (cmd[1] == '1');
        if (cmd[0] == 'R') set_rgb(on,  g_g, g_b);
        if (cmd[0] == 'G') set_rgb(g_r, on,  g_b);
        if (cmd[0] == 'B') set_rgb(g_r, g_g, on );
        uart1_puts("OK\r\n");
        return;
    }

    // --- Session -----------------------------------------------------------

    if (!strcmp(cmd, "HELP")) {
        uart1_puts("OK PING VERSION UPTIME STATE TEMP HUMID LIGHT "
                   "FAN0 FAN1 FAN_AUTO FANTHRESH:XX "
                   "MOTION_ARM MOTION_DISARM "
                   "BUZZ:X BUZZ0 "
                   "LDR_AUTO LDR_MAN:XX "
                   "LED_ON LED_OFF LED_BRIGHT:XX "
                   "RGB:xyz R0/R1 G0/G1 B0/B1 X EXIT\r\n");
        return;
    }

    if (!strcmp(cmd, "EXIT")) {
        uart1_puts("OK DISCONNECTED\r\n");
        g_connected = false;
        return;
    }

    uart1_puts("ERR\r\n");
}

// =============================================================================
// main
// =============================================================================

int main(void)
{
    // --- Clock: 16 MHz main oscillator ------------------------------------
    SysCtlClockSet(SYSCTL_SYSDIV_1  |
                   SYSCTL_USE_OSC   |
                   SYSCTL_OSC_MAIN  |
                   SYSCTL_XTAL_16MHZ);

    g_reset_cause = SysCtlResetCauseGet();
    SysCtlResetCauseClear(g_reset_cause);

    // --- Peripheral init --------------------------------------------------
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}
    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE,
                          GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    set_rgb(0, 0, 0);

    // GPIOE: ADC needs PE2 (LDR), DHT11 needs PE3 — both enabled in adc0_init
    adc0_init();

    // DHT11 starts as input with external pull-up holding line HIGH (idle state)
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOE);
    while (!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOE)) {}
    dht11_pin_input();

    uart1_init_9600();
    pir_init();
    buzzer_init();
    fan_init();
    ws2812_init();      // must come after pir_init (shares GPIOD)
    systick_init();

    IntMasterEnable();

    // Clear strip on boot — ensures all LEDs start off
    ws2812_clear();

    // --- HC-05 boot delay (blue LED = waiting) ----------------------------
    set_rgb(0, 0, 1);
    SysCtlDelay(16000000UL * 2 / 3);   // ~2 s @ 16 MHz
    set_rgb(0, 0, 0);

    // --- Watchdog armed after boot delay ----------------------------------
    watchdog_init();

    // --- Startup message --------------------------------------------------
    if (g_reset_cause & SYSCTL_CAUSE_WDOG0)
        uart1_puts("EVT WDT_RESET\r\n");

    uart1_puts("SYSTEM STATUS: READY\r\n"
               "FW 0.5 | DHT11 + WS2812B | ENTER A COMMAND OR 'HELP'\r\n");

    // Initial sensor read — sent to host immediately on connect
    // DHT11 needs a moment after power-on before first read is reliable
    SysCtlDelay(16000000UL / 3);       // 1 s additional DHT11 settle time
    if (dht11_read() == 0) {
        uart1_puts("STARTUP TEMP=");
        uart1_put_i16(g_dht_temp_f);
        uart1_puts("F HUMID=");
        uart1_put_u32(g_dht_humid);
        uart1_puts("% LIGHT=");
        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
        uart1_puts("\r\n");
    } else {
        uart1_puts("STARTUP TEMP=ERR HUMID=ERR LIGHT=");
        uart1_put_u32(adc_to_light_pct(adc0_read_ldr()));
        uart1_puts("\r\n");
    }

    // --- Main loop --------------------------------------------------------
    char line[CMD_BUF_SIZE];

    while (1)
    {
        watchdog_pet();

        // ── Motion event ─────────────────────────────────────────────────────
        // PIR ISR sets g_motion_evt; 5-second cooldown prevents bounce spam.
        //
        // First trigger:  double-flash (2× 150 ms on/off) → steady white 25 s
        //                 → dim to 40% for final 5 s → strip off. Total: ~30 s.
        // Re-trigger:     if strip is already steady or dimming, silently reset
        //                 the 30-second timer and restore full brightness.
        //                 No flash — avoids annoying flicker for someone
        //                 already in the room.
        if (g_motion_evt) {
            g_motion_evt = false;
            if (g_tick_ms - g_motion_last_ms >= MOTION_COOLDOWN_MS) {
                g_motion_last_ms = g_tick_ms;
                uart1_puts("EVT MOTION\r\n");
                g_buzz_until_ms = g_tick_ms + 300;

                if (g_led_motion_state >= LED_MSTATE_STEADY) {
                    // Re-trigger: restore full brightness, reset timer, no flash
                    g_led_bright = 100;
                    ws2812_show();
                    g_led_dim_ms = g_tick_ms + LED_DIM_AFTER_MS;
                    g_led_off_ms = g_tick_ms + LED_OFF_AFTER_MS;
                    g_led_motion_state = LED_MSTATE_STEADY;
                } else {
                    // First trigger: start double-flash sequence
                    g_led_bright = 100;
                    led_on();
                    g_led_phase_end_ms = g_tick_ms + LED_FLASH_MS;
                    g_led_dim_ms       = g_tick_ms + (4 * LED_FLASH_MS) + LED_DIM_AFTER_MS;
                    g_led_off_ms       = g_tick_ms + (4 * LED_FLASH_MS) + LED_OFF_AFTER_MS;
                    g_led_motion_state = LED_MSTATE_FLASH1_ON;
                }
            }
        }

        // ── Motion LED state machine ──────────────────────────────────────────
        switch (g_led_motion_state) {
            case LED_MSTATE_FLASH1_ON:
                if (g_tick_ms >= g_led_phase_end_ms) {
                    ws2812_clear();
                    g_led_on           = false;
                    g_led_motion_state = LED_MSTATE_FLASH1_OFF;
                    g_led_phase_end_ms += LED_FLASH_MS;
                }
                break;
            case LED_MSTATE_FLASH1_OFF:
                if (g_tick_ms >= g_led_phase_end_ms) {
                    g_led_bright = 100;
                    led_on();
                    g_led_motion_state = LED_MSTATE_FLASH2_ON;
                    g_led_phase_end_ms += LED_FLASH_MS;
                }
                break;
            case LED_MSTATE_FLASH2_ON:
                if (g_tick_ms >= g_led_phase_end_ms) {
                    ws2812_clear();
                    g_led_on           = false;
                    g_led_motion_state = LED_MSTATE_FLASH2_OFF;
                    g_led_phase_end_ms += LED_FLASH_MS;
                }
                break;
            case LED_MSTATE_FLASH2_OFF:
                if (g_tick_ms >= g_led_phase_end_ms) {
                    // Flash complete — hold steady white
                    g_led_bright = 100;
                    led_on();
                    g_led_motion_state = LED_MSTATE_STEADY;
                }
                break;
            case LED_MSTATE_STEADY:
                if (g_tick_ms >= g_led_dim_ms) {
                    // 25 s elapsed — dim to 40% as "turning off soon" warning
                    // Short beep alerts occupant that the strip is about to turn off
                    g_led_bright = LED_DIM_BRIGHT;
                    ws2812_show();
                    g_buzz_until_ms    = g_tick_ms + 200;   // 200 ms warning beep
                    g_led_motion_state = LED_MSTATE_DIM;
                }
                break;
            case LED_MSTATE_DIM:
                if (g_tick_ms >= g_led_off_ms) {
                    // 30 s elapsed — turn off and notify host
                    led_off();
                    uart1_puts("EVT LED_OFF\r\n");
                }
                break;
            default:
                break;
        }

        // Buzzer timer
        if (g_buzz_until_ms > 0) {
            if (g_tick_ms < g_buzz_until_ms)
                buzzer_set(true);
            else {
                buzzer_set(false);
                g_buzz_until_ms = 0;
            }
        }

        // Periodic auto logic
        if (g_tick_ms - g_last_poll_ms >= POLL_INTERVAL_MS) {
            g_last_poll_ms = g_tick_ms;
            run_auto_logic();
        }

        // UART command handler
        if (g_connected && read_line_uart1(line, sizeof(line)))
            handle_cmd(line);
    }
}
