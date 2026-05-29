// Лабораторная работа №4 — драйвер UART на регистрах ATmega328 (Вариант 2)
// Формат кадра: 38400 бод, 8 бит данных, odd parity, 2 стоп-бита (8-O-2).
// Шаг 3: приём/проверка/эхо пакетов + раз в секунду отправка данных BMP280 (SPI).
// Ссылки на datasheet ATmega328P (раздел 19. USART0).

#include <SPI.h>
#include <Adafruit_BMP280.h>  // Library Manager -> "Adafruit BMP280"

// Установить бит `bita` в регистре `reg`
#define SetBit(reg, bita) reg |= (1 << bita)

#define SYNC_BYTE 0x5A        // синхробайт начала пакета (слайд 16)
#define MAX_DATA  255         // поле длины 1 байт => максимум 255 байт данных
#define BMP_CS    10          // CS датчика (hardware SPI: SCK=13, MISO=12, MOSI=11)

Adafruit_BMP280 bmp(BMP_CS);  // BMP280 на аппаратном SPI

// --- Состояния конечного автомата разбора потока ---------------------------
enum RxState { S_SYNC, S_LEN, S_DATA, S_CRC };

volatile RxState rxState = S_SYNC;  // текущее состояние приёмника
volatile uint8_t rxData[MAX_DATA];  // буфер принимаемых данных
volatile uint8_t rxLen = 0;         // ожидаемая длина данных (из поля len)
volatile uint8_t rxIdx = 0;         // сколько байт данных уже принято

// Очередь на отправку валидного пакета обратно (заполняется в ISR, шлётся в loop)
volatile bool    packetReady = false;
volatile uint8_t outData[MAX_DATA];
volatile uint8_t outLen = 0;

unsigned long lastSensorMs = 0;     // момент последней отправки данных датчика

// CRC8 (полином 0x07, init 0x00) — считается по байтам [len + data].
// ВАЖНО: тот же алгоритм должен быть в клиенте на ПК, иначе пакеты не сойдутся.
uint8_t crc8(uint8_t len, volatile uint8_t *data) {
  uint8_t crc = 0x00;
  crc ^= len;                                   // включаем байт длины
  for (uint8_t i = 0; i < 8; i++)
    crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);

  for (uint8_t b = 0; b < len; b++) {           // включаем байты данных
    crc ^= data[b];
    for (uint8_t i = 0; i < 8; i++)
      crc = (crc & 0x80) ? (crc << 1) ^ 0x07 : (crc << 1);
  }
  return crc;
}

// Отправить один байт: ждём освобождения буфера передатчика (UDRE0 в UCSR0A,
// стр. 159), затем пишем в регистр данных UDR0 (стр. 159) — это запускает передачу.
void uartSendByte(uint8_t b) {
  while (!(UCSR0A & (1 << UDRE0)));
  UDR0 = b;
}

// Собрать и отправить пакет: S | len | data | CRC8
void uartSendPacket(volatile uint8_t *data, uint8_t len) {
  uartSendByte(SYNC_BYTE);            // синхробайт
  uartSendByte(len);                  // длина
  for (uint8_t i = 0; i < len; i++)   // данные
    uartSendByte(data[i]);
  uartSendByte(crc8(len, data));      // контрольная сумма
}

void setup() {
  // --- Скорость 38400 бод (UBRR = f_osc/(16*BAUD)-1 = 25, стр. 146 / 162) ----
  uint16_t baudRate = 38400;
  uint16_t ubrr = 16000000 / 16 / baudRate - 1;
  UBRR0H = (unsigned char)(ubrr >> 8);  // старший байт скорости (стр. 162)
  UBRR0L = (unsigned char)ubrr;         // младший байт скорости (стр. 162)

  // --- Включение приёмника/передатчика и прерывания приёма (UCSR0B, стр. 160) -
  SetBit(UCSR0B, TXEN0);   // разрешить передатчик
  SetBit(UCSR0B, RXEN0);   // разрешить приёмник
  SetBit(UCSR0B, RXCIE0);  // прерывание по завершению приёма байта

  // --- Формат кадра (UCSR0C, стр. 160-162) ----------------------------------
  SetBit(UCSR0C, UCSZ01);  // \ 8 бит данных (UCSZ1:0=11, Table 19-7, стр. 162)
  SetBit(UCSR0C, UCSZ00);  // /
  SetBit(UCSR0C, UPM01);   // \ odd parity (UPM1:0=11, Table 19-5, стр. 161)
  SetBit(UCSR0C, UPM00);   // /
  SetBit(UCSR0C, USBS0);   // 2 стоп-бита (USBS=1, Table 19-6, стр. 161)

  // --- Датчик BMP280 по SPI -------------------------------------------------
  bmp.begin();                                       // инициализация датчика
  bmp.setSampling(Adafruit_BMP280::MODE_NORMAL,      // режим непрерывных измерений
                  Adafruit_BMP280::SAMPLING_X2,      // передискретизация температуры
                  Adafruit_BMP280::SAMPLING_X16,     // передискретизация давления
                  Adafruit_BMP280::FILTER_X16,       // фильтрация
                  Adafruit_BMP280::STANDBY_MS_500);
}

// Прерывание "приём завершён" — конечный автомат разбора потока на пакеты
ISR(USART_RX_vect) {
  uint8_t byte = UDR0;  // прочитать принятый байт (UDR0, стр. 159)

  switch (rxState) {
    case S_SYNC:  // ждём синхробайт; всё прочее игнорируем
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

    case S_CRC:   // последний байт — контрольная сумма
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

// Прочитать датчик и отправить пакет с температурой и давлением.
// Данные: 8 байт = float температура (°C) + float давление (Па), little-endian.
void sendSensorPacket() {
  uint8_t data[8];
  float temperature = bmp.readTemperature();  // °C
  float pressure    = bmp.readPressure();     // Па

  memcpy(&data[0], &temperature, 4);  // первые 4 байта — температура
  memcpy(&data[4], &pressure, 4);     // следующие 4 байта — давление
  uartSendPacket(data, 8);
}

void loop() {
  if (packetReady) {                       // есть валидный пакет на отправку
    uartSendPacket(outData, outLen);       // шлём его обратно на ПК (эхо)
    packetReady = false;                   // освободить очередь
  }

  // раз в секунду шлём данные датчика (без delay, чтобы эхо не задерживалось)
  if (millis() - lastSensorMs >= 1000) {
    lastSensorMs = millis();
    sendSensorPacket();
  }
}
