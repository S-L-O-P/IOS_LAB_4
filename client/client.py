import sys
import time
import struct
import serial

PORT = sys.argv[1] if len(sys.argv) > 1 else "/dev/ttyUSB0"
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
    while ser.read(1) != bytes([SYNC]):
        pass
    length = ser.read(1)[0]
    data = ser.read(length)
    crc = ser.read(1)[0]
    if crc != crc8(bytes([length]) + data):
        return None
    return data


def collect_packets(ser, duration):
    # читать корректные пакеты в течение duration секунд -> список их data
    end = time.time() + duration
    found = []
    while time.time() < end:
        if ser.read(1) != bytes([SYNC]):      # ждём синхробайт
            continue
        length = ser.read(1)
        if not length:
            continue
        length = length[0]
        data = ser.read(length)
        crc = ser.read(1)
        if len(data) != length or not crc:     # пакет не дочитался
            continue
        if crc[0] == crc8(bytes([length]) + data):
            found.append(data)
    return found


def run_tests(ser):
    # (название, сырые байты для отправки, отличимые data, ждём ли эхо)
    correct = build_packet(b"TEST")
    tests = [
        ("Корректный пакет",   correct,                                          b"TEST", True),
        ("Нет синхробайта",    correct[1:],                                      b"TEST", False),
        ("Неправильная длина", bytes([SYNC, 2]) + b"WLEN" + bytes([crc8(b"\x02WLEN")]), b"WLEN", False),
        ("Недостаточно данных", bytes([SYNC, 8]) + b"AB",                         b"AB",   False),
        ("Битый CRC",          correct[:-1] + bytes([correct[-1] ^ 0xFF]),       b"TEST", False),
    ]

    for name, raw, payload, expect_echo in tests:
        time.sleep(2)                 # дать МК ресинхронизироваться (пакет датчика)
        ser.reset_input_buffer()
        ser.write(raw)
        got_echo = payload in collect_packets(ser, 1.5)
        verdict = "ПРИНЯТ" if got_echo else "ОТБРОШЕН"
        ok = "OK" if got_echo == expect_echo else "FAIL"
        print(f"{name:22} -> {verdict:9} [{ok}]")


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

    print("=== Тест пакетов ===")
    run_tests(ser)

    print("=== Данные датчика (Ctrl+C для выхода) ===")
    while True:
        data = read_packet(ser)
        if data is None:
            print("битый пакет")
        elif len(data) == 8:
            temp, press = struct.unpack("<ff", data)
            print(f"T = {temp:.2f}   P = {press:.1f}")
        else:
            print("data:", data.hex(" "))


if __name__ == "__main__":
    main()
