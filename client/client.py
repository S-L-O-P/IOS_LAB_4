import sys
import time
import struct
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
MODE = sys.argv[2] if len(sys.argv) > 2 else "echo"
BAUD = 38400
SYNC = 0x5A


def crc8(data):
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def build_packet(data):
    body = bytes([len(data)]) + data
    return bytes([SYNC]) + body + bytes([crc8(body)])


def read_packet(ser):
    """Прочитать из порта один пакет S|len|data|CRC. None — если CRC не сошёлся."""
    while ser.read(1) != bytes([SYNC]):  # ждём синхробайт
        pass
    length = ser.read(1)[0]              # байт длины
    data = ser.read(length)             # данные
    crc = ser.read(1)[0]                # контрольная сумма
    if crc != crc8(bytes([length]) + data):
        return None                     # битый пакет
    return data


def echo_test(ser):
    """Отправить корректный пакет и сверить эхо."""
    packet = build_packet(b"hello")
    print("TX:", packet.hex(" "))
    ser.write(packet)
    echo = ser.read(len(packet))
    print("RX:", echo.hex(" "))
    print("OK" if echo == packet else "ERROR")


def listen(ser):
    """Слушать пакеты от МК и декодировать данные датчика (temp + давление)."""
    print("Listening... (Ctrl+C для выхода)")
    while True:
        data = read_packet(ser)
        if data is None:
            print("битый пакет")
        elif len(data) == 8:                      # 2 float: температура + давление
            temp, press = struct.unpack("<ff", data)
            print(f"T = {temp:.2f} *C   P = {press:.1f} Pa")
        else:
            print("data:", data.hex(" "))


def main():
    ser = serial.Serial(
        PORT, BAUD,
        bytesize=serial.EIGHTBITS,
        parity=serial.PARITY_ODD,
        stopbits=serial.STOPBITS_TWO,
        timeout=2,
    )
    time.sleep(2)
    ser.reset_input_buffer()

    if MODE == "listen":
        listen(ser)
    elif MODE == "raw":                  # диагностика: печатать все входящие байты
        print("Raw dump... (Ctrl+C для выхода)")
        while True:
            d = ser.read(64)
            if d:
                print(d.hex(" "))
    else:
        echo_test(ser)

    ser.close()


if __name__ == "__main__":
    main()
