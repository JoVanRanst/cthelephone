# SPDX-FileCopyrightText: 2022 Espressif Systems (Shanghai) CO LTD
# SPDX-License-Identifier: Unlicense OR CC0-1.0
import os
import struct
import wave

try:
    from typing import List
except ImportError:
    pass


def get_wave_array_str(filename, target_bits):  # type: (str, int) -> str
    wave_read = wave.open(filename, 'r')
    array_str = ''
    nchannels, sampwidth_bytes, framerate, nframes, comptype, compname = wave_read.getparams()
    sampwidth = sampwidth_bytes * 8
    for i in range(wave_read.getnframes()):
        frame = wave_read.readframes(1)
        if nchannels == 2:
            # Stereo: extract left channel
            if sampwidth == 16:
                left, _ = struct.unpack('<hh', frame)  # signed 16-bit
                val = left
            elif sampwidth == 8:
                left, _ = struct.unpack('<BB', frame)  # unsigned 8-bit
                val = left
            else:
                raise ValueError(f"Unsupported sample width: {sampwidth}")
        else:
            # Mono: use sample directly
            if sampwidth == 16:
                val, = struct.unpack('<h', frame)  # signed 16-bit
            elif sampwidth == 8:
                val, = struct.unpack('<B', frame)  # unsigned 8-bit
            else:
                raise ValueError(f"Unsupported sample width: {sampwidth}")

        # Convert to unsigned 8-bit
        if sampwidth == 16:
            # 16-bit signed PCM: -32768..32767 -> 0..255
            val = int((val + 32768) * 255 / 65535)
        elif sampwidth == 8:
            # 8-bit unsigned PCM: 0..255 (no change needed)
            val = int(val)
        else:
            raise ValueError(f"Unsupported sample width: {sampwidth}")

        val = max(0, min(255, val))  # Clamp to 0..255
        array_str += '0x%02x, ' % (val)
        if (i + 1) % 16 == 0:
            array_str += '\n'
    return array_str


def gen_wave_table(wav_file_list, target_file_name, scale_bits=8):  # type: (List[str], str, int) -> None
    with open(target_file_name, 'w') as audio_table:
        print('#include <stdio.h>', file=audio_table)
        print('#define CONFIG_AUDIO_SAMPLE_RATE 48000', file=audio_table)
        for wav in wav_file_list:
            print('const unsigned char {}_audio_table[] = {{'.format(os.path.splitext(os.path.basename(wav))[0]), file=audio_table)
            print('processing: {}'.format(wav))
            print(get_wave_array_str(filename=wav, target_bits=scale_bits), file=audio_table)
            print('};\n', file=audio_table)
    print('Done...')


if __name__ == '__main__':
    print('Generating audio array...')
    wav_list = []
    for wavefile in os.listdir('./'):
        if wavefile.endswith('.wav'):
            wav_list.append(wavefile)
    gen_wave_table(wav_file_list=wav_list, target_file_name='audio_file.h')
