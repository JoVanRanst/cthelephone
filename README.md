| Hardware Target | ESP32 | CHEAP-YELLOW-DISPLAY |
| --------------- | ----- | -------------------- |
# Cthulhu rotary telephone

This project is part of a art installation that brings several characters to live from the H.P. Lovecraft novels, but from the view of how their lives would look like in our modern day.

One of the works, called 'calling Cthulhu'. Through an old rotary phone place near the work, visistors can call with him directly, getting a joke from him.

## Hardware

The main ingrediants are:
- A CYD (Cheap-Yellow-Display), which allready contains a ESP32 and a audio amp.
- Connectors for the audio and the CN1 connector.
- An old rotary phone

Optionally: adding in a smith trigger circuit on the rotary lines helps with stability

## Software

The core is a state machine that runs through each possible state.
....

## How to use the Example

### Configure the Project

This example uses the audio that stored in a buffer, which is put in `audio_example_file.h`. You can also create your own audio buffer by the python script `generate_audio_file.py`.

### Build and Flash

Build the project and flash it to the board, then run monitor tool to view serial output:

```
idf.py -p PORT flash monitor
```

(Replace PORT with the name of the serial port to use.)

(To exit the serial monitor, type ``Ctrl-]``.)

See the Getting Started Guide for full steps to configure and use ESP-IDF to build projects.

## Example Output

You can see the following logs on the monitor:

```
I (277) dac audio: DAC audio example start
I (277) dac audio: --------------------------------------
I (287) dac audio: DAC initialized success, DAC DMA is ready
I (297) dac audio: Audio size 79512 bytes, played at frequency 16000 Hz
I (5137) dac audio: Audio size 79512 bytes, played at frequency 16000 Hz
I (9967) dac audio: Audio size 79512 bytes, played at frequency 16000 Hz
