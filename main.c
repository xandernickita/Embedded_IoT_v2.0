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

#define RX_BUF_SIZE 256

volatile char rx_buf[RX_BUF_SIZE];
volatile uint16_t rx_head = 0;
volatile uint16_t rx_tail = 0;

uint32_t g_start_time; // global counter

void UART1_Handler(void);

static uint8_t g_r = 0, g_g = 0, g_b = 0;
static bool g_connected = true;

static void uart1_put_u32(uint32_t v)
{
    char tmp[11];              // max 10 digits for uint32 + null
    int i = 0;

    if (v == 0) {
        UARTCharPut(UART1_BASE, '0');
        return;
    }

    while (v > 0 && i < 10) {
        tmp[i++] = (char)('0' + (v % 10));
        v /= 10;
    }

    while (i--) {
        UARTCharPut(UART1_BASE, tmp[i]);
    }
}

static void uart1_puts(const char *s)
{
    while (*s)
    {
        UARTCharPut(UART1_BASE, *s++);
    }
}

static char bit_char(uint8_t v) { return v ? '1' : '0'; }

static void set_rgb(uint8_t r, uint8_t g, uint8_t b)
{
    g_r = r ? 1 : 0;
    g_g = g ? 1 : 0;
    g_b = b ? 1 : 0;

    uint8_t val = 0;
    if (g_r) val |= GPIO_PIN_1; // Red
    if (g_b) val |= GPIO_PIN_2; // Blue
    if (g_g) val |= GPIO_PIN_3; // Green

    GPIOPinWrite(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3, val);
}

static void uart1_init_9600(void)
{
    // Enable UART1 and GPIOB (UART1 pins are on Port B: PB0=U1RX, PB1=U1TX)
    SysCtlPeripheralEnable(SYSCTL_PERIPH_UART1);
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOB);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_UART1)) {}
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOB)) {}

    // Configure PB0/PB1 for UART1
    GPIOPinConfigure(GPIO_PB0_U1RX);
    GPIOPinConfigure(GPIO_PB1_U1TX);
    GPIOPinTypeUART(GPIO_PORTB_BASE, GPIO_PIN_0 | GPIO_PIN_1);

    // UART config: 9600 8N1 (HC-05 default data mode is typically 9600)
    UARTConfigSetExpClk(UART1_BASE, SysCtlClockGet(), 9600,
                        (UART_CONFIG_WLEN_8 | UART_CONFIG_STOP_ONE | UART_CONFIG_PAR_NONE));

    UARTEnable(UART1_BASE);

    UARTIntRegister(UART1_BASE, UART1_Handler);
    UARTIntEnable(UART1_BASE, UART_INT_RX | UART_INT_RT);
}

static int read_line_uart1(char *buf, int max_len)
{
    static int i = 0;

    while (rx_tail != rx_head)
    {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) % RX_BUF_SIZE;

        if (c == '\r' || c == '\n')
        {
            if (i > 0)
            {
                buf[i] = '\0';
                i = 0;
                return 1;
            }
            continue;
        }

        if (i < max_len - 1)
            buf[i++] = c;
    }

    return 0;
}

static void handle_cmd(const char *cmd)
{
    if (!strcmp(cmd, "UPTIME"))
    {
        uart1_puts("OK ");
        uart1_put_u32(SysCtlClockGet());
        uart1_puts("\r\n");
        return;
    }

    // PING tests
    if (!strcmp(cmd, "PING"))
    {
        uart1_puts("OK PONG\r\n");
        return;
    }

    // List firmware version
    if (!strcmp(cmd, "VERSION"))
    {
        uart1_puts("OK FW 0.1\r\n");
        return;
    }

    // EXIT / exit
    if ((cmd[0] == 'E' || cmd[0] == 'e') &&
        (cmd[1] == 'X' || cmd[1] == 'x') &&
        (cmd[2] == 'I' || cmd[2] == 'i') &&
        (cmd[3] == 'T' || cmd[3] == 't') &&
        cmd[4] == '\0')
    {
        uart1_puts("OK DISCONNECTED\r\n");
        uart1_puts("CLOSE TERMINAL TO RECONNECT\r\n");

        g_connected = false;

        return;
    }

    // Basic commands:
    // R0/R1, G0/G1, B0/B1, X
    // New commands:
    // HELP
    // STATE
    // RGB:xyz   where x,y,z are 0/1 (R,G,B)

    // HELP
    if (cmd[0] == 'H' && cmd[1] == 'E' && cmd[2] == 'L' && cmd[3] == 'P' && cmd[4] == '\0')
    {
        uart1_puts("OK Commands: R0/R1 G0/G1 B0/B1 X RGB:xyz STATE HELP\r\n");
        return;
    }

    // STATE
    if (cmd[0] == 'S' && cmd[1] == 'T' && cmd[2] == 'A' && cmd[3] == 'T' && cmd[4] == 'E' && cmd[5] == '\0')
    {
        uart1_puts("OK RGB=");
        UARTCharPut(UART1_BASE, bit_char(g_r));
        UARTCharPut(UART1_BASE, bit_char(g_g));
        UARTCharPut(UART1_BASE, bit_char(g_b));
        uart1_puts("\r\n");
        return;
    }

    // X = all off
    if (cmd[0] == 'X' && cmd[1] == '\0')
    {
        set_rgb(0,0,0);
        uart1_puts("OK\r\n");
        return;
    }

    // RGB:xyz (exactly 7 chars: R G B bits)
    // Example: RGB:101 -> R=1, G=0, B=1
    if (cmd[0] == 'R' && cmd[1] == 'G' && cmd[2] == 'B' && cmd[3] == ':' &&
        (cmd[4] == '0' || cmd[4] == '1') &&
        (cmd[5] == '0' || cmd[5] == '1') &&
        (cmd[6] == '0' || cmd[6] == '1') &&
        cmd[7] == '\0')
    {
        uint8_t r = (cmd[4] == '1');
        uint8_t g = (cmd[5] == '1');
        uint8_t b = (cmd[6] == '1');
        set_rgb(r,g,b);
        uart1_puts("OK\r\n");
        return;
    }

    // Single-channel commands
    if ((cmd[0] == 'R' || cmd[0] == 'G' || cmd[0] == 'B') &&
        (cmd[1] == '0' || cmd[1] == '1') &&
        cmd[2] == '\0')
    {
        uint8_t on = (cmd[1] == '1');

        if (cmd[0] == 'R') set_rgb(on, g_g, g_b);
        if (cmd[0] == 'G') set_rgb(g_r, on, g_b);
        if (cmd[0] == 'B') set_rgb(g_r, g_g, on);

        uart1_puts("OK\r\n");
        return;
    }

    uart1_puts("ERR\r\n");
}

void UART1_Handler(void)
{
    uint32_t status = UARTIntStatus(UART1_BASE, true);
    UARTIntClear(UART1_BASE, status);

    while (UARTCharsAvail(UART1_BASE))
    {
        char c = UARTCharGetNonBlocking(UART1_BASE);
        uint16_t next = (rx_head + 1) % RX_BUF_SIZE;

        if (next != rx_tail)   // prevent overflow
        {
            rx_buf[rx_head] = c;
            rx_head = next;
        }
    }
}

int main(void)
{
    // 16 MHz main osc
    SysCtlClockSet(SYSCTL_SYSDIV_1 |
                   SYSCTL_USE_OSC |
                   SYSCTL_OSC_MAIN |
                   SYSCTL_XTAL_16MHZ);

    // init clock
    g_start_time = SysCtlClockGet();

    // Enable Port F for RGB LED
    SysCtlPeripheralEnable(SYSCTL_PERIPH_GPIOF);
    while(!SysCtlPeripheralReady(SYSCTL_PERIPH_GPIOF)) {}

    GPIOPinTypeGPIOOutput(GPIO_PORTF_BASE, GPIO_PIN_1 | GPIO_PIN_2 | GPIO_PIN_3);
    set_rgb(0,0,0);

    // Init UART1 for HC-05
    uart1_init_9600();
    IntMasterEnable();

    uart1_puts("SYSTEM STATUS: READY\r\nPLEASE ENTER A COMMAND (OR 'HELP' FOR MORE OPTIONS)\r\n");

    char line[16];

    while(1)
    {
        if (g_connected && read_line_uart1(line, sizeof(line)))
        {
            handle_cmd(line);
        }
    }

}

