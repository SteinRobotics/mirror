# RP2040 Mirror

Mirrors all data received via UART2, SPI, and I2C to the USB Serial interface.

## Interfaces

| Interface  | Role   | Description                     |
| ---------- | ------ | ------------------------------- |
| USB Serial | Output | 115200 baud, mirror output      |
| UART2      | Input  | 115200 baud (Serial2 = UART1)   |
| I2C        | Slave  | Address `0x30` (Wire = I2C0)    |
| SPI        | Slave  | Mode 0, MSB first, 1 MHz (SPI0) |

## Pin Assignment

| Function      | Pin  | GPIO |
| ------------- | ---- | ---- |
| UART2 TX      | GP8  | 8    |
| UART2 RX      | GP9  | 9    |
| I2C SDA       | GP4  | 4    |
| I2C SCL       | GP5  | 5    |
| SPI RX (MOSI) | GP16 | 16   |
| SPI CS        | GP17 | 17   |
| SPI SCK       | GP18 | 18   |
| SPI TX (MISO) | GP19 | 19   |

## Output Format

Each received byte is printed to USB Serial with a tag, hex value, and ASCII character (if printable):

```
[UART2] 0x48 'H'
[I2C]   0x65 'e'
[SPI]   0xFF
```
