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


def read_packet(ser):
    while ser.read(1) != bytes([SYNC]):
        pass
    length = ser.read(1)[0]
    data = ser.read(length)
    crc = ser.read(1)[0]
    if crc != crc8(bytes([length]) + data):
        return None
    return data


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
