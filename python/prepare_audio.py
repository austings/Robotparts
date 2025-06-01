with open("hello.wav", "rb") as f:
    data = f.read()[44:]  # Skip WAV header
    with open("/dev/ttyACM0", "wb") as pico:
        pico.write(data)
