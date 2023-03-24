# Introduction
Applications that use the OFDM and DAB code.

## Application list
| Name | Description |
| --- | --- |
| **radio_app** | **The complete radio app with controls for the tuner** |
| bin/rtl_sdr | Reads raw 8bit IQ values from your rtl-sdr dongle to stdout |
| basic_radio_app | Complete app that reads raw 8bit IQ stream and demodulates and decodes it into a basic radio |
| basic_radio_app_no_demod | Reads in a digital OFDM frame from ofdm_demod_gui or ofdm_demod_cli and decodes it for a basic radio |
| ofdm_demod_cli | Demodulates a raw 8bit IQ stream into digital OFDM frames |
| ofdm_demod_gui | Demodulates a raw 8bit IQ stream into digital OFDM frames with a GUI |
| convert_viterbi | Decodes/encodes between a viterbi_bit_t array of soft decision bits to a packed byte |
| basic_radio_scraper | Reads raw 8bit IQ stream, demodulates and decodes the data, then saves it to disk |
| basic_radio_scraper_no_demod | Reads in a digital OFDM frame from ofdm_demod_gui or ofdm_demod_cli, decodes the data then saves it to disk |
| simulate_transmitter | Simulates a OFDM signal with a defined transmission mode, but doesn't contain any meaningful digital data. Outputs an 8bit IQ stream to stdout. |
| apply_frequency_shift | Applies a frequency shift to a 8bit IQ stream |
| read_wav | Reads in a wav file which can be 8bit or 16bit PCM and dumps raw data to output as 8bit |

## Example usage scenarios (using git-bash on Windows)
### 1. Run the complete radio app with rtlsdr tuner controls
<code>./radio_app.exe</code>

### 2. Run the radio app on a DAB ensemble while reading from rtl_sdr.exe

<code>./get_live_data.sh -c 9C | ./basic_radio_app.exe</code>

### 3. Run data scraper

<code>./get_live_data.sh -c 9C | ./basic_radio_scraper.exe -o ./data/9C_2/</code>

### 4. Run OFDM demod while saving undecoded OFDM frame bits

<code>./get_live_data.sh -c 9C | ./ofdm_demod_gui.exe > ./data/frame_bits_9C.bin</code>

### 5. Run OFDM demod while saving undecoded OFDM frame as packed bytes for 8x less space

<code>./get_live_data.sh -c 9C | ./ofdm_demod_gui.exe | ./convert_viterbi.exe > ./data/frame_bytes_9C.bin</code>

### 6. Unpack packed bytes into OFDM frame bits and run DAB radio app

<code>./convert_viterbi.exe -d -i ./data/frame_bytes_9C.bin | ./basic_radio_app_no_demod.exe</code>

**NOTE**: This is useful for testing changes to your DAB decoding implementation by replaying packed byte OFDM frames that you recorded previously. 

### 7. Play radio with GUI while storing OFDM frames as packed bytes

<code>./get_live_data.sh -c 9C | ./ofdm_demod_gui.exe | ./convert_viterbi.exe | tee data/frame_bytes_9C.bin | ./convert_viterbi.exe -d | ./basic_radio_app_no_demod.exe</code>

**NOTE**: <code>tee</code> is a unix application that reads in stdin and outputs to stdout and the specified filepath.

### 8. Run data scraper with OFDM demodulator GUI while storing OFDM frames as packed bytes

<code>./get_live_data.sh -c 9C | ./ofdm_demod_gui.exe | ./convert_viterbi.exe | tee data/frame_bytes_9C.bin | ./convert_viterbi.exe -d | ./basic_radio_scraper_no_demod.exe</code>
