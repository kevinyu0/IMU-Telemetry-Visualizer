import socket
from vpython import box, vector, rate, color, radians

board = box(length=5, width=0.5, height=3, color=color.blue)

HOST = "" 
PORT = 8888
BUFFER_SIZE = 1024

alpha = 0.9; # Lower alpha means more smoothing, higher alpha means less smoothing
filtered_pitch = 0.0
filtered_yaw = 0.0
filtered_roll = 0.0

# Creates a UDP socket to listen for data from the ESP32
sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
sock.bind((HOST, PORT))
sock.setblocking(False)  # Sets socket to non-blocking mode so it doesn't freeze the program while waiting for data

print(f"Listening for ESP32 telemetry on port {PORT}...")

while True:
    rate(60) # Limits visualization updates to 60 frames per second to reduce CPU usage

    latest_data = None

    # Drains buffer to get the latest data packet, ignoring any older packets that may have accumulated
    try:
        while True:
            data, addr = sock.recvfrom(BUFFER_SIZE)
            latest_data = data
    except BlockingIOError:
        pass

    if latest_data:
        try:
            message = latest_data.decode("utf-8")
            parts = message.split(",")

            if len(parts) == 3:
                pitch = float(parts[0])
                yaw = float(parts[1])
                roll = float(parts[2])

                # Low-pass filter to reduce jitter in the visualization
                filtered_pitch = alpha * pitch + (1 - alpha) * filtered_pitch
                filtered_yaw = alpha * yaw + (1 - alpha) * filtered_yaw
                filtered_roll = alpha * roll + (1 - alpha) * filtered_roll

                # Snaps board to initial orientation before applying rotations
                board.axis = vector(1, 0, 0)
                board.up = vector(0, 1, 0)

                board.rotate(axis=vector(1,0,0), angle=radians(filtered_pitch))
                board.rotate(axis=vector(0,1,0), angle=radians(filtered_yaw))
                board.rotate(axis=vector(0,0,1), angle=radians(filtered_roll))

        except ValueError:
            pass # Ignores corrupted data to continue running the program without crashing
