#include <SPI.h>
#include <Adafruit_BMP280NS.h> 

#define SetBit(reg, bita) reg |= (1 << bita)

#define SYNC_BYTE 0x5A
#define MAX_DATA 255
#define BMP_CS 10          // CS датчика (hardware SPI: SCK=13, MISO=12, MOSI=11)

Adafruit_BMP280 bmp(BMP_CS);  // BMP280 на аппаратном SPI

enum RxState { S_SYNC, S_LEN, S_DATA, S_CRC };

volatile RxState rxState = S_SYNC;  // текущее состояние приёмника
volatile uint8_t rxData[MAX_DATA]; // буфер принимаемых данных
volatile uint8_t rxLen = 0; // ожидаемая длина данных (из поля len)
volatile uint8_t rxIdx = 0; // сколько байт данных уже принято

volatile bool packetReady = false;
volatile uint8_t outData[MAX_DATA];
volatile uint8_t outLen = 0;

unsigned long lastSensorMs = 0;

uint8_t crc8(uint8_t len, volatile uint8_t *data) {
  uint8_t crc = 0x00;
  crc ^= len;
  for (uint8_t i = 0; i < 8; i++)
    crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);

  for (uint8_t b = 0; b < len; b++) {
    crc ^= data[b];
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}

// ждём освобождения буфера передатчика (UDRE0 в UCSR0A стр. 159), затем пишем в регистр данных UDR0 (стр. 159) — это запускает передачу.
void uartSendByte(uint8_t b) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = b;
}

// Собрать и отправить пакет: S | len | data | CRC8
void uartSendPacket(volatile uint8_t *data, uint8_t len) {
  uartSendByte(SYNC_BYTE); // синхробайт
  uartSendByte(len); // длина
  for (uint8_t i = 0; i < len; i++) // данные
    uartSendByte(data[i]);
  uartSendByte(crc8(len, data)); // контрольная сумма
}

void setup() {
  uint16_t baudRate = 38400;
  uint16_t ubrr = 16000000 / 16 / baudRate - 1;
  UBRR0H = (unsigned char)(ubrr >> 8);  
  UBRR0L = (unsigned char) ubrr; // стр. 162

  SetBit(UCSR0B, TXEN0);
  SetBit(UCSR0B, RXEN0); 
  SetBit(UCSR0B, RXCIE0); 

  SetBit(UCSR0C, UCSZ01); // 8 бит данных стр. 162
  SetBit(UCSR0C, UCSZ00);  
  SetBit(UCSR0C, UPM01); // odd parity стр. 161
  SetBit(UCSR0C, UPM00);   
  SetBit(UCSR0C, USBS0); // 2 стоп-бита стр. 161

  bmp.begin(); // инициализация датчика
}

// Interrupt Service Routine
ISR(USART_RX_vect) {
  uint8_t byte = UDR0;  // прочитать принятый байт стр. 159

  switch (rxState) {
    case S_SYNC:
      if (byte == SYNC_BYTE) rxState = S_LEN;
      break;

    case S_LEN:   // байт длины
      rxLen = byte;
      rxIdx = 0;
      rxState = (rxLen == 0) ? S_CRC : S_DATA;  // len=0 => сразу ждём CRC
      break;

    case S_DATA:  // копим данные
      rxData[rxIdx++] = byte;
      if (rxIdx >= rxLen) rxState = S_CRC;
      break;

    case S_CRC:   // последний байт - контрольная сумма
      // принят валидный пакет и предыдущий уже отправлен -> ставим в очередь эхо
      if (byte == crc8(rxLen, rxData) && !packetReady) {
        for (uint8_t i = 0; i < rxLen; i++) outData[i] = rxData[i];
        outLen = rxLen;
        packetReady = true;
      }
      // битый пакет просто отбрасывается (ничего не делаем)
      rxState = S_SYNC;  // в любом случае ждём следующий пакет
      break;
  }
}

void sendSensorPacket() {
  uint8_t data[8];
  float temperature = bmp.readTemperature(); // градусы
  float pressure = bmp.readPressure(); // Па

  memcpy(&data[0], &temperature, 4); // первые 4 байта - температура
  memcpy(&data[4], &pressure, 4); // следующие 4 байта - давление
  uartSendPacket(data, 8);
}

void loop() {
  if (packetReady) { // есть валидный пакет на отправку
    uartSendPacket(outData, outLen); // шлём его обратно на ПК (эхо)
    packetReady = false; 
  }

  if (millis() - lastSensorMs >= 1000) {
    lastSensorMs = millis();
    sendSensorPacket();
  }
}
