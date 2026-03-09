#include <Arduino.h>
#include <Wire.h>
#include <SPISlave.h>
#include <hardware/gpio.h>

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

// Debug Pin (external task toggle)
#define DEBUG_PIN 2
#define DEBUG_REPORT_INTERVAL 500 // ms
static constexpr uint32_t DBG_CYCLE_WARN_US = 110;
static constexpr uint32_t DBG_LOAD_WARN_PERMILLE = 800; // 80.0%

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

static char uartRxBuf[BUF_SIZE];
static size_t uartRxLen = 0;

// Debug pin timing (ISR → main)
static volatile uint32_t dbgLastRiseUs = 0;
static volatile uint32_t dbgLastPeriodUs = 0;
static volatile uint32_t dbgMaxCycleUs = 0;      // max cycle time (rise-to-rise)
static volatile uint32_t dbgMaxLoadPermille = 0; // max load in 0.1% units
static volatile bool dbgActive = false;
static uint32_t dbgLastReport = 0;

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

void onDebugPinIRQ(uint gpio, uint32_t events)
{
  uint32_t now = micros();
  if (events & GPIO_IRQ_EDGE_RISE)
  {
    // Rising edge: measure cycle time (rise-to-rise)
    if (dbgActive)
    {
      uint32_t cycle = now - dbgLastRiseUs;
      dbgLastPeriodUs = cycle;
      if (cycle > dbgMaxCycleUs)
        dbgMaxCycleUs = cycle;
    }
    dbgLastRiseUs = now;
    dbgActive = true;
  }
  if (events & GPIO_IRQ_EDGE_FALL)
  {
    // Falling edge: measure task duration (high time) -> load
    if (dbgActive)
    {
      uint32_t taskUs = now - dbgLastRiseUs;
      if (dbgLastPeriodUs > 0)
      {
        uint32_t load = taskUs * 1000 / dbgLastPeriodUs;
        if (load > dbgMaxLoadPermille)
          dbgMaxLoadPermille = load;
      }
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

  // Debug Pin (use Pico SDK for reliable edge detection)
  gpio_init(DEBUG_PIN);
  gpio_set_dir(DEBUG_PIN, GPIO_IN);
  gpio_set_irq_enabled_with_callback(DEBUG_PIN,
                                     GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, true, &onDebugPinIRQ);
  Serial.printf("  DEBUG  : GP%d (input, edge interrupt)\n", DEBUG_PIN);

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
    uint8_t b = Serial2.read();
    if (b == '\r')
    {
      Serial.print("[UART] ");
      Serial.write(reinterpret_cast<const uint8_t *>(uartRxBuf), uartRxLen);
      Serial.println();
      uartRxLen = 0;
    }
    else if (b != '\n' && uartRxLen < BUF_SIZE - 1)
    {
      uartRxBuf[uartRxLen++] = static_cast<char>(b);
    }
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

  // Report debug pin timing every 500ms
  if (millis() - dbgLastReport >= DEBUG_REPORT_INTERVAL)
  {
    dbgLastReport = millis();
    noInterrupts();
    uint32_t maxCyc = dbgMaxCycleUs;
    uint32_t maxLd = dbgMaxLoadPermille;
    dbgMaxCycleUs = 0;
    dbgMaxLoadPermille = 0;
    interrupts();
    if (dbgActive && (maxCyc > DBG_CYCLE_WARN_US || maxLd > DBG_LOAD_WARN_PERMILLE))
    {
      Serial.printf("[DBG] max cycle=%luus  max load=%lu.%lu%%\n",
                    (unsigned long)maxCyc,
                    (unsigned long)(maxLd / 10),
                    (unsigned long)(maxLd % 10));
    }
  }

  // Forward CR-terminated messages to UART2.
  while (Serial.available())
  {
    uint8_t b = Serial.read();

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