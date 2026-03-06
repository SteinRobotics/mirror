# RP2040 Mirror

Mirrors all data received via UART2, SPI, and I2C to the USB Serial interface.

## Interfaces

| Interface  | Role   | Description                     |
| ---------- | ------ | ------------------------------- |
| USB Serial | Output | 921600 baud, mirror output      |
| UART2      | Input  | 921600 baud (Serial2 = UART1)   |
| I2C        | Slave  | Address `0x30` (Wire = I2C0)    |
| SPI        | Slave  | Mode 0, MSB first, 1 MHz (SPI0) |

## Pin Assignment

The current pin config is valid for a Raspberry Pi Pico:

- `UART2` in the sketch uses `Serial2`, which maps to `UART1` on `GP8`/`GP9`
- `I2C` uses `I2C0` on `GP4`/`GP5`
- `SPI` uses `SPI0` on `GP16`-`GP19`

| Function      | Pico Pin | GPIO |
| ------------- | -------- | ---- |
| UART2 TX      | 11       | GP8  |
| UART2 RX      | 12       | GP9  |
| I2C SDA       | 6        | GP4  |
| I2C SCL       | 7        | GP5  |
| SPI RX (MOSI) | 21       | GP16 |
| SPI CS        | 22       | GP17 |
| SPI SCK       | 24       | GP18 |
| SPI TX (MISO) | 25       | GP19 |

## Ground And Power Pins

### Ground Pins

| Pico Pin | Signal |
| -------- | ------ |
| 3        | GND    |
| 8        | GND    |
| 13       | GND    |
| 18       | GND    |
| 23       | GND    |
| 28       | GND    |
| 33       | AGND   |
| 38       | GND    |

### VCC / Supply Pins

| Pico Pin | Signal   | Notes                        |
| -------- | -------- | ---------------------------- |
| 35       | ADC_VREF | ADC reference supply         |
| 36       | 3V3(OUT) | Regulated 3.3 V output       |
| 39       | VSYS     | Main system supply input     |
| 40       | VBUS     | USB 5 V input from USB cable |

## Output Format

Each received byte is printed to USB Serial with a tag, hex value, and ASCII character (if printable):

```
[UART2] 0x48 'H'
[I2C]   0x65 'e'
[SPI]   0xFF
```
