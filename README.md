# Teensy 4.1 Digital Capture & Playback with SD Storage

This project uses a **Teensy 4.1** microcontroller to:
1. **Capture** a high-speed digital signal at 300 kHz for ~50 ms (~15,000 samples) on a specified input pin.
2. **Store** the captured samples in **RAM**.
3. **Save** the samples from RAM to the **SD card** so they persist across resets.
4. **Load** saved recordings from the SD card back into RAM for playback.
5. **Playback** the digital signal on an output pin at the same 300 kHz rate.
6. **Wait** for an input pin to go **HIGH** or **LOW** before starting a capture.
7. **Loop** playback as many times as desired, with a user-configurable pause between each repetition.

By default, the project is configured to:
- Use **pin A0** for the digital input signal (adjust if needed).
- Use **pin A9** for playback output (adjust if needed).
- Sample at **300 kHz** for a **50 ms** capture window (up to 15,000 samples).
- Store each capture as a raw byte array (1 byte per sample) plus a small header on the SD card (e.g., `capture0.bin`, `capture1.bin`, etc.).

## Hardware Requirements

- **Teensy 4.1** (for built-in SD card slot and sufficient RAM).
- An **SD card** inserted in the Teensy 4.1 SD slot.
- A **digital signal** source connected to the capture pin (default A0).
- (Optional) Additional wiring or test signals for playback on pin A9.

## Installation & Setup

1. **Clone or download** this repository.
2. Open the `.ino` sketch in the **Arduino IDE** (or PlatformIO, etc.).
3. Select **Teensy 4.1** as the target board.
4. Confirm the **SD library** is installed (the built-in Arduino SD library or an alternative like SdFat).
5. (Optional) Adjust `CAPTURE_PIN`, `OUTPUT_PIN`, or the `SAMPLE_RATE` in the code if necessary.
6. **Upload** the code to the Teensy 4.1.
7. Open the **Serial Monitor** at **115200 baud** and type the commands listed below.

## Command Reference

Type any of these commands into the Serial Monitor and press **Enter**:

- **`READHIGH`**  
  Waits until the **capture pin** goes **HIGH**, then begins a 50 ms capture at 300 kHz. Prints a message when capture starts and ends.

- **`READLOW`**  
  Waits until the **capture pin** goes **LOW**, then begins a 50 ms capture at 300 kHz. Prints a message when capture starts and ends.

- **`SAVE`**  
  Writes the latest captured data (in RAM) to an **auto-incremented file** on the SD card (e.g., `capture0.bin`, `capture1.bin`, etc.). Also stores the number of samples as a small header in the file.

- **`PLAY`**  
  Plays back the **latest** capture (in RAM) on the output pin. If no capture is in RAM, a warning is shown.

- **`PLAY N`**  
  Loads the file **`captureN.bin`** from the SD card into RAM, then plays it on the output pin. For example, **`PLAY 3`** loads `capture3.bin`.

- **`LOOP`**  
  Toggles **playback looping** ON or OFF. When looping is ON, the code automatically restarts playback after it finishes, waiting for the user-defined pause interval in between.

- **`PAUSE <ms>`**  
  Sets the **pause** (in milliseconds) to wait **after** a playback finishes, **before** restarting again (when looping is ON). For example, **`PAUSE 1000`** creates a 1-second gap between loops.

- **`STOP`**  
  Immediately stops any capture or playback in progress.

- **`FORMAT`**  
  Deletes all files matching **`capture*.bin`** on the SD card (i.e., `capture0.bin`, `capture1.bin`, etc.).

## Notes & Tips

- Teensy 4.1 has **512 KB** of RAM, so a single 15 KB buffer is manageable. If you need to store multiple simultaneous buffers in RAM, watch for memory usage.
- **High-speed captures** may require more advanced methods (DMA, FlexIO) if your project demands longer capture durations or higher rates.
- If you use different pins, adjust `CAPTURE_PIN`, `OUTPUT_PIN`, and confirm the pin is suitable for digital reads/writes.
- Modify the `SAMPLE_RATE` or `NUM_SAMPLES` if you want a different capture duration or sampling speed (but ensure your code and hardware can keep up).
- This is a **proof-of-concept**; in production, you may need robust error handling, file management, or advanced buffering.

---

**Enjoy capturing and playing back signals with Teensy 4.1 and SD card storage!**
