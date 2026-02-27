import serial
import time

def main():
    port = input("Enter COM port (ex: COM7): ").strip()
    ser = serial.Serial(port, 9600, timeout=1)
    time.sleep(1)

    print("Commands: R1 R0 G1 G0 B1 B0 X Q")

    while True:
        cmd = input("> ").strip().upper()
        if cmd == "Q":
            break
        ser.write((cmd + "\n").encode())

    ser.close()

if __name__ == "__main__":
    main()
