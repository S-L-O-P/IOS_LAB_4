import sys


def crc8(data):
    crc = 0x00
    for byte in data:
        crc ^= byte
        for _ in range(8):
            crc = ((crc << 1) ^ 0x07) & 0xFF if (crc & 0x80) else (crc << 1) & 0xFF
    return crc


def main():
    # байты пакета из аргументов: "5A 04 54 45 53 54 F5" (с 0x или без, любой регистр)
    args = " ".join(sys.argv[1:]).replace("0x", "").replace(",", " ").split()
    packet = [int(x, 16) for x in args]

    if len(packet) < 3:
        print("Использование: python3 crc_check.py 5A 04 54 45 53 54 F5")
        return

    sync, length = packet[0], packet[1]
    data = packet[2:-1]
    crc_got = packet[-1]
    crc_calc = crc8([length] + data)   # CRC по [длина + данные]

    print(f"Синхробайт : 0x{sync:02X}" + ("" if sync == 0x5A else "  <- ожидался 0x5A!"))
    print(f"Длина      : {length}" + ("" if length == len(data) else f"  <- данных по факту {len(data)}!"))
    print(f"Данные     : {' '.join(f'{b:02X}' for b in data)}")
    print(f"CRC принят : 0x{crc_got:02X}")
    print(f"CRC расчёт : 0x{crc_calc:02X}")
    print("=> CRC сошёлся, пакет целый" if crc_got == crc_calc else "=> CRC НЕ сошёлся, пакет битый")


if __name__ == "__main__":
    main()
