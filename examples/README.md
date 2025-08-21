# Introduction
Applications that use the OFDM and DAB code.

## Application list
| Name | Description |
| --- | --- |
| **radio_app** | **The complete radio app with controls for the tuner** |
| rtl_sdr | Reads raw 8bit IQ values from your rtl-sdr dongle to stdout |
| basic_radio_app | OFDM demodulator and/or radio decoder that reads from a file with a gui |
| basic_radio_app_cli | OFDM demodulator and/or radio decoder that reads from a file without a gui |
| apply_frequency_shift | Applies a frequency shift to a 8bit IQ stream |
| convert_viterbi | Decodes/encodes between a viterbi_bit_t array of soft decision bits to a packed byte |
| simulate_transmitter | Simulates a OFDM signal with a defined transmission mode, but doesn't contain any meaningful digital data. Outputs an unsigned 8bit IQ stream to stdout. |
| loop_file | Loop file infinitely (can be a raw binary file or .wav file) |

## Example usage scenarios (using git-bash on Windows)
Refer to ```-h``` or ```--help``` for more information on each application.

### GUI Radio app with built in rtlsdr tuner controls
```./radio_app```

### Tuner => OFDM => Radio => Audio
```./rtl_sdr -c [CHANNEL] | ./basic_radio_app```

### Tuner => OFDM => Radio => Audio & Scraper
```./rtl_sdr -c [CHANNEL] | ./basic_radio_app --scraper-enable --scraper-output [DIRECTORY]```

### Tuner => OFDM => File_Soft
```./rtl_sdr -c [CHANNEL] | ./basic_radio_app --configuration ofdm --ofdm-enable-output > [FILENAME]```

### File_Soft => Radio => Audio
```./basic_radio_app -i [FILENAME] --configuration dab```

### Tuner => OFDM => Soft_to_Hard => File_Hard
```./rtl_sdr -c [CHANNEL] | ./basic_radio_app --configuration ofdm --ofdm-enable-output | ./convert_viterbi --type soft_to_hard > [FILENAME]```

An OFDM frame consists of 8bits values that represent a number from -127 to +127. We can instead represent them as -1 or +1 as a single bit. This reduces the amount of space by 8 times.

### File_Hard => Hard_to_Soft => Radio => Audio
```./convert_viterbi -i [FILENAME] | ./basic_radio_app --configuration dab```

### Tuner => OFDM => (Soft_to_Hard => File_Hard), (Radio => Audio)
```./rtl_sdr -c [CHANNEL] | ./basic_radio_app --configuration ofdm --ofdm-enable-output | tee >(./convert_viterbi --type soft_to_hard > [FILENAME]) | ./basic_radio_app --configuration dab```

- ```tee [...]``` is a command that copies stdin to multiple output files and stdout.
- ```>([command])``` is process substitution for bash shells. The command can then be used as a file descriptor (such as an argument for ```tee```).
