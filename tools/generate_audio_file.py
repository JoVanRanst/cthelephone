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
    nchannels, sampwidth, framerate, nframes, comptype, compname = wave_read.getparams()
    sampwidth *= 8
    for i in range(wave_read.getnframes()):
        frame = wave_read.readframes(1)
        if nchannels == 2:
            # Stereo: extract left channel
            if sampwidth == 16:
                left, _ = struct.unpack('<HH', frame)
                val = left
            elif sampwidth == 8:
                left, _ = struct.unpack('<BB', frame)
                val = left
            else:
                raise ValueError(f"Unsupported sample width: {sampwidth}")
        else:
            # Mono: use sample directly
            if sampwidth == 16:
                val, = struct.unpack('<H', frame)
            elif sampwidth == 8:
                val, = struct.unpack('<B', frame)
            else:
                raise ValueError(f"Unsupported sample width: {sampwidth}")
        scale_val = (1 << target_bits) - 1
        cur_lim   = (1 << sampwidth) - 1
        # scale current data to 8-bit data
        val       = val * scale_val / cur_lim
        val       = int(val + ((scale_val + 1) // 2)) & scale_val
        array_str += '0x%x, ' % (val)
        if (i + 1) % 16 == 0:
            array_str += '\n'
    return array_str


def gen_wave_table(wav_file_list, target_file_name, scale_bits=8):  # type: (List[str], str, int) -> None
    with open(target_file_name, 'w') as audio_table:
        print('#include <stdio.h>', file=audio_table)
        print('#define CONFIG_AUDIO_SAMPLE_RATE 24000', file=audio_table)
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
