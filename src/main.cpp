#include <Arduino.h>
#include <Wire.h>
#include <SPISlave.h>

// ─── Pin Definitions ───────────────────────────────────────
// UART2 (Serial2 = UART1)
#define UART2_TX_PIN 8
#define UART2_RX_PIN 9

// I2C Slave (Wire = I2C0)
#define I2C_SDA_PIN 4
#define I2C_SCL_PIN 5
#define I2C_SLAVE_ADDR 0x30

// SPI Slave (SPI0)
#define SPI_RX_PIN 16 // MOSI (data from master)
#define SPI_CS_PIN 17
#define SPI_SCK_PIN 18
#define SPI_TX_PIN 19 // MISO (data to master)

// LED
#define LED_BLINK_INTERVAL 500 // ms

// Serial baud rates
#define USB_SERIAL_BAUD 921600
#define UART2_BAUD 921600

// USB Serial -> UART2 bridge
#define USB_MSG_BUF_SIZE 128

// ─── Ring Buffers (ISR → main) ─────────────────────────────
#define BUF_SIZE 512

static volatile uint8_t i2cBuf[BUF_SIZE];
static volatile uint16_t i2cHead = 0;
static volatile uint16_t i2cTail = 0;

static volatile uint8_t spiBuf[BUF_SIZE];
static volatile uint16_t spiHead = 0;
static volatile uint16_t spiTail = 0;

static uint32_t ledLastToggle = 0;
static char usbMsgBuf[USB_MSG_BUF_SIZE];
static size_t usbMsgLen = 0;
static bool usbMsgOverflow = false;

// ─── Callbacks ─────────────────────────────────────────────
void onI2CReceive(int numBytes)
{
  while (Wire.available())
  {
    uint8_t b = Wire.read();
    uint16_t next = (i2cHead + 1) % BUF_SIZE;
    if (next != i2cTail)
    {
      i2cBuf[i2cHead] = b;
      i2cHead = next;
    }
  }
}

void onSPIReceive(uint8_t *data, size_t len)
{
  for (size_t i = 0; i < len; i++)
  {
    uint16_t next = (spiHead + 1) % BUF_SIZE;
    if (next != spiTail)
    {
      spiBuf[spiHead] = data[i];
      spiHead = next;
    }
  }
}

// ─── Helpers ───────────────────────────────────────────────
static void printByte(const char *tag, uint8_t b)
{
  Serial.printf("%s 0x%02X", tag, b);
  if (b >= 0x20 && b <= 0x7E)
  {
    Serial.printf(" '%c'", (char)b);
  }
  Serial.println();
}

static void flushUsbMessage()
{
  if (usbMsgOverflow)
  {
    Serial.println("[USB]   message too long, discarded");
    usbMsgLen = 0;
    usbMsgOverflow = false;
    return;
  }

  if (usbMsgLen == 0)
  {
    return;
  }

  Serial.print("[USB->UART2] ");
  Serial.write(reinterpret_cast<const uint8_t *>(usbMsgBuf), usbMsgLen);
  Serial.println();

  Serial2.write(reinterpret_cast<const uint8_t *>(usbMsgBuf), usbMsgLen);
  Serial2.write('\r');
  Serial2.write('\n');

  usbMsgLen = 0;
}

// ─── Setup ─────────────────────────────────────────────────
void setup()
{
  // LED
  pinMode(LED_BUILTIN, OUTPUT);

  // USB Serial (output)
  Serial.begin(USB_SERIAL_BAUD);
  while (!Serial && millis() < 3000)
    ;

  Serial.println("=== RP2040 Mirror ===");
  Serial.println("Mirroring UART2, SPI (slave), I2C (slave) -> USB Serial");
  Serial.println();

  // UART2 (Serial2)
  Serial2.setTX(UART2_TX_PIN);
  Serial2.setRX(UART2_RX_PIN);
  Serial2.begin(UART2_BAUD);
  Serial.printf("  UART2  : TX=GP%d  RX=GP%d  %lu baud\n",
                UART2_TX_PIN, UART2_RX_PIN, (unsigned long)UART2_BAUD);

  // I2C Slave
  Wire.setSDA(I2C_SDA_PIN);
  Wire.setSCL(I2C_SCL_PIN);
  Wire.begin(I2C_SLAVE_ADDR);
  Wire.onReceive(onI2CReceive);
  Serial.printf("  I2C    : SDA=GP%d SCL=GP%d addr=0x%02X\n", I2C_SDA_PIN, I2C_SCL_PIN, I2C_SLAVE_ADDR);

  // SPI Slave
  SPISlave.setRX(SPI_RX_PIN);
  SPISlave.setTX(SPI_TX_PIN);
  SPISlave.setSCK(SPI_SCK_PIN);
  SPISlave.setCS(SPI_CS_PIN);
  SPISlave.onDataRecv(onSPIReceive);
  SPISlave.begin(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  Serial.printf("  SPI    : RX=GP%d  TX=GP%d  SCK=GP%d  CS=GP%d\n",
                SPI_RX_PIN, SPI_TX_PIN, SPI_SCK_PIN, SPI_CS_PIN);

  Serial.println();
  Serial.println("Ready - waiting for data ...");
  Serial.println();
}

// ─── Loop ──────────────────────────────────────────────────
void loop()
{
  // Blink LED
  if (millis() - ledLastToggle >= LED_BLINK_INTERVAL)
  {
    ledLastToggle = millis();
    digitalWrite(LED_BUILTIN, !digitalRead(LED_BUILTIN));
  }

  // Mirror UART2
  while (Serial2.available())
  {
    printByte("[UART2]", Serial2.read());
  }

  // Mirror I2C
  while (i2cHead != i2cTail)
  {
    uint8_t b = i2cBuf[i2cTail];
    i2cTail = (i2cTail + 1) % BUF_SIZE;
    printByte("[I2C]  ", b);
  }

  // Mirror SPI
  while (spiHead != spiTail)
  {
    uint8_t b = spiBuf[spiTail];
    spiTail = (spiTail + 1) % BUF_SIZE;
    printByte("[SPI]  ", b);
  }

  // Echo USB Serial locally and forward CR-terminated messages to UART2.
  while (Serial.available())
  {
    uint8_t b = Serial.read();
    Serial.write(b);

    if (b == '\n')
    {
      continue;
    }

    if (b == '\r')
    {
      flushUsbMessage();
      continue;
    }

    if (usbMsgOverflow)
    {
      continue;
    }

    if (usbMsgLen >= USB_MSG_BUF_SIZE - 1)
    {
      usbMsgOverflow = true;
      continue;
    }

    usbMsgBuf[usbMsgLen++] = static_cast<char>(b);
  }
}