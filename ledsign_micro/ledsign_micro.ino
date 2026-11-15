#include <Arduino.h>
#include <string.h>

// Arduino Micro: UART framebuffer receiver and 90x7 LED display driver.
// Pico W sends: 0xAA, 0x55, 90 column bytes, XOR checksum.
// Micro replies: 0x06 (ACK) or 0x15 (NAK).

// Display geometry and refresh timing
constexpr uint8_t DISPLAY_WIDTH = 90;
constexpr uint8_t DISPLAY_HEIGHT = 7;
constexpr uint8_t GROUP_COUNT = 6;
constexpr uint8_t COLUMNS_PER_GROUP = 15;
constexpr unsigned int COLUMN_US = 750;

// UART protocol (must match the Pico W)
constexpr unsigned long UART_BAUD = 57600;
constexpr uint8_t FRAME_START_1 = 0xAA;
constexpr uint8_t FRAME_START_2 = 0x55;
constexpr uint8_t ACK = 0x06;
constexpr uint8_t NAK = 0x15;
constexpr unsigned long RECEIVE_TIMEOUT_MS = 200;

// Verified Arduino Micro wiring: do not change without remapping hardware.
const uint8_t DATA_PINS[7] = {2, 3, 4, 5, 6, 7, 8};
constexpr uint8_t RESET_PIN = 9;
constexpr uint8_t G2A_PIN = A0;
constexpr uint8_t C_PIN = A1;
constexpr uint8_t B_PIN = A2;
constexpr uint8_t A_PIN = A3;
constexpr uint8_t G2B_PIN = A4;

// Verified physical row-to-register mapping.
const uint8_t ROW_BUS_BITS[7] = {5, 6, 4, 1, 2, 0, 3};
const uint8_t PHYSICAL_ROW_TO_Q[7] = {5, 3, 4, 6, 2, 0, 1};

// Each framebuffer byte represents one physical column (bits 0-6 = rows).
uint8_t framebuffer[DISPLAY_WIDTH] = {};
uint8_t columnData[COLUMNS_PER_GROUP][GROUP_COUNT] = {};

// A complete received frame is applied at the start of the next refresh.
uint8_t readyFrame[DISPLAY_WIDTH];
bool frameReady = false;

uint8_t incomingFrame[DISPLAY_WIDTH];
uint8_t incomingCount = 0;
uint8_t incomingChecksum = 0;
uint8_t receiveState = 0;
unsigned long lastReceivedByte = 0;

void receiveFrames();

// ----- Original display hardware -----

void disableDecoder() {
  digitalWrite(G2A_PIN, HIGH);
  digitalWrite(G2B_PIN, HIGH);
}

void setAddress(uint8_t address) {
  digitalWrite(A_PIN, (address & 1) ? HIGH : LOW);
  digitalWrite(B_PIN, (address & 2) ? HIGH : LOW);
  digitalWrite(C_PIN, (address & 4) ? HIGH : LOW);
}

void selectDecoder(uint8_t address) {
  disableDecoder();
  setAddress(address);
  delayMicroseconds(5);
  digitalWrite(G2A_PIN, LOW);
  digitalWrite(G2B_PIN, LOW);
}

void writeBus(uint8_t value) {
  for (uint8_t bit = 0; bit < 7; bit++) {
    digitalWrite(DATA_PINS[bit], (value & (1 << bit)) ? HIGH : LOW);
  }
}

void writeRowRegister(uint8_t group, uint8_t data) {
  disableDecoder();
  writeBus(data);
  delayMicroseconds(5);
  selectDecoder(group);
  delayMicroseconds(5);
  disableDecoder();
}

void writeColumnBit(uint8_t latch, uint8_t bit, bool value) {
  disableDecoder();
  // P10 = data; P11-P13 = bit address (A0-A2).
  uint8_t bus = (bit & 7) << 1;
  if (value) bus |= 1;
  writeBus(bus);
  delayMicroseconds(5);
  selectDecoder(6 + latch);
  delayMicroseconds(5);
  disableDecoder();
}

void allRowsOff() {
  for (uint8_t group = 0; group < GROUP_COUNT; group++) {
    writeRowRegister(group, 0);
  }
}

void allColumnsOff() {
  for (uint8_t latch = 0; latch < 2; latch++) {
    for (uint8_t bit = 0; bit < 8; bit++) {
      writeColumnBit(latch, bit, HIGH);
    }
  }
}

void setColumn(uint8_t column, bool on) {
  writeColumnBit(column / 8, column % 8, on ? LOW : HIGH);
}

void updateColumnData() {
  for (uint8_t group = 0; group < GROUP_COUNT; group++) {
    for (uint8_t col = 0; col < COLUMNS_PER_GROUP; col++) {
      const uint8_t x = group * COLUMNS_PER_GROUP + col;
      const uint8_t pixels = framebuffer[x];
      uint8_t rowBus = 0;

      for (uint8_t row = 0; row < DISPLAY_HEIGHT; row++) {
        if (pixels & (1 << row)) {
          const uint8_t q = PHYSICAL_ROW_TO_Q[row];
          rowBus |= (1 << ROW_BUS_BITS[q]);
        }
      }
      columnData[col][group] = rowBus;
    }
  }
}

// ----- UART frame receiver -----

void receiveFrames() {
  if (receiveState != 0 && millis() - lastReceivedByte > RECEIVE_TIMEOUT_MS) {
    receiveState = 0;
    incomingCount = 0;
  }

  while (Serial1.available() > 0) {
    const uint8_t b = Serial1.read();
    lastReceivedByte = millis();

    switch (receiveState) {
      case 0:  // Wait for first header byte
        if (b == FRAME_START_1) receiveState = 1;
        break;

      case 1:  // Wait for second header byte
        if (b == FRAME_START_2) {
          receiveState = 2;
          incomingCount = 0;
          incomingChecksum = 0;
        } else {
          receiveState = (b == FRAME_START_1) ? 1 : 0;
        }
        break;

      case 2:  // Read exactly 90 column bytes
        incomingFrame[incomingCount++] = b;
        incomingChecksum ^= b;
        if (incomingCount == DISPLAY_WIDTH) receiveState = 3;
        break;

      case 3:  // Check XOR checksum and acknowledge
        if (b == incomingChecksum) {
          memcpy(readyFrame, incomingFrame, DISPLAY_WIDTH);
          frameReady = true;
          Serial1.write(ACK);
        } else {
          Serial1.write(NAK);
        }
        receiveState = 0;
        incomingCount = 0;
        break;

      default:
        receiveState = 0;
        incomingCount = 0;
        break;
    }
  }
}

// ----- Multiplex refresh -----

void refreshDisplay() {
  if (frameReady) {
    memcpy(framebuffer, readyFrame, DISPLAY_WIDTH);
    updateColumnData();
    frameReady = false;
  }

  for (uint8_t col = 0; col < COLUMNS_PER_GROUP; col++) {
    const uint8_t previousCol = (col == 0) ? COLUMNS_PER_GROUP - 1 : col - 1;
    setColumn(previousCol, false);

    for (uint8_t group = 0; group < GROUP_COUNT; group++) {
      writeRowRegister(group, columnData[col][group]);
    }

    setColumn(col, true);
    delayMicroseconds(COLUMN_US);

    // Essential: drain UART throughout refresh to prevent RX buffer overflow.
    receiveFrames();
  }
}

void setup() {
  Serial1.begin(UART_BAUD);

  // Set inactive levels before enabling outputs.
  digitalWrite(G2A_PIN, HIGH);
  digitalWrite(G2B_PIN, HIGH);
  digitalWrite(RESET_PIN, HIGH);

  pinMode(G2A_PIN, OUTPUT);
  pinMode(G2B_PIN, OUTPUT);
  pinMode(RESET_PIN, OUTPUT);
  pinMode(A_PIN, OUTPUT);
  pinMode(B_PIN, OUTPUT);
  pinMode(C_PIN, OUTPUT);

  for (uint8_t pin : DATA_PINS) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }

  disableDecoder();
  digitalWrite(RESET_PIN, LOW);
  delay(10);
  digitalWrite(RESET_PIN, HIGH);
  delay(10);

  allRowsOff();
  allColumnsOff();
  updateColumnData();  // Start with a blank display until the first Pico frame.
}

void loop() {
  refreshDisplay();
  receiveFrames();
}
