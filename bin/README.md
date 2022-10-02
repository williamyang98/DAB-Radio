## Explanation
This directory contains the compiled binary for rtl_sdr.exe which is available [here](https://github.com/osmocom/rtl-sdr/tree/master/src).

This version of rtl_sdr contains a minor modification which includes copying over argument parsing from rtl_fm. This allows for selecting direct2 which is direct quadrature sampling from Q branch. This is required on the rtl_sdr blog V3 dongle to enable direct sampling.