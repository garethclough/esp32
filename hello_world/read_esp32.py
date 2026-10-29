import socket

ESP32_IP = "10.194.53.79"  # Change this
PORT = 5000

with socket.create_connection((ESP32_IP, PORT), timeout=5) as sock:
    sock.sendall(b"READ\n")

    data = sock.makefile("r").readline()
    print("ESP32 reading:", data.strip())