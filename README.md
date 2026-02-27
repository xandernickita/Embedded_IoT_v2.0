# HC-05 Module Interface

Embedded + desktop control project for a **TM4C123GH6PM (Tiva C)** board using an **HC-05 Bluetooth serial module**.

The firmware exposes a simple UART command protocol to control the onboard RGB LED, and the Python GUI provides a user-friendly Bluetooth/COM interface.

## Project Structure

- `main.c`: TM4C123 firmware (UART command parser + RGB LED control)
- `tm4c123gh6pm_startup_ccs.c`: startup/interrupt vector file
- `tm4c123gh6pm.cmd`: linker command file
- `tiva_bt_gui.py`: Tkinter desktop app for COM/Bluetooth control
- `bluetooth_led_controller.py`: minimal CLI serial command sender
- `.project`, `.cproject`, `.ccsproject`: Code Composer Studio project files
- `targetConfigs/`: CCS target configuration

## Features

- UART1 command interface for LED control
- Commands for `HELP`, `STATE`, `PING`, `VERSION`, `UPTIME`, and `EXIT`
- RGB control via:
  - Single-channel commands: `R0/R1`, `G0/G1`, `B0/B1`
  - Combined command: `RGB:xyz` (example: `RGB:101`)
  - All off: `X`
- Desktop GUI with:
  - COM port scanning
  - Connect/disconnect status
  - RGB toggles + color presets
  - Manual command entry
  - Response log

## Hardware/Signal Notes

- MCU: TM4C123GH6PM
- Bluetooth module: HC-05 in data mode
- UART used in firmware: **UART1 @ 9600 baud**
  - PB0 = U1RX
  - PB1 = U1TX
- Onboard RGB LED pins used:
  - PF1 = Red
  - PF2 = Blue
  - PF3 = Green

Ensure HC-05 TX/RX wiring matches MCU RX/TX correctly.

## Firmware Build and Flash (CCS)

1. Open project in Code Composer Studio.
2. Ensure TivaWare include/library paths are valid in project properties.
3. Build the `Debug` configuration.
4. Flash and run on the TM4C123 board.

On boot, firmware prints:
- `SYSTEM STATUS: READY`

## Python GUI Usage

### Requirements

- Python 3.9+ recommended
- `pyserial`

Install dependency:

```powershell
pip install pyserial
```

### Run

```powershell
python tiva_bt_gui.py
```

Use the GUI to:
1. Select the HC-05 COM port
2. Connect at `9600` baud
3. Send preset or manual commands

## Command Protocol

All commands are line-based and terminated by `\n` or `\r\n`.

### Input Commands

- `HELP`
- `STATE`
- `PING`
- `VERSION`
- `UPTIME`
- `EXIT`
- `X`
- `R0`, `R1`
- `G0`, `G1`
- `B0`, `B1`
- `RGB:xyz` where each of `x`, `y`, `z` is `0` or `1`

### Typical Responses

- `OK`
- `OK PONG`
- `OK FW 0.1`
- `OK RGB=xyz`
- `OK <value>` (for `UPTIME` in current firmware behavior)
- `ERR`

## Quick Test (CLI)

You can test commands without the GUI:

```powershell
python bluetooth_led_controller.py
```

Then send commands interactively (example: `R1`, `RGB:011`, `STATE`).

## Troubleshooting

- No COM port visible:
  - Verify HC-05 pairing and assigned COM port in Windows
  - Re-open GUI and click **Refresh Ports**
- Commands not affecting LED:
  - Confirm UART1 wiring (TX/RX crossed correctly)
  - Confirm baud is `9600`
  - Check board power and firmware is running
- Garbled or no response:
  - Verify matching line endings and baud rate
  - Confirm only one app is connected to the COM port at a time

## License

No license file is currently included in this repository.
