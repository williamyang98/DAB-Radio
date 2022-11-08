## Explanation
This directory contains the compiled binary for rtl_sdr.exe which is available [here](https://github.com/osmocom/rtl-sdr/tree/master/src).

This version of rtl_sdr contains the following modifications: 
- Added option for direct sampling
- Added option for enabling bias tee
- Fixed line 220: Set stdout instead of stdin to binary mode which stops Windows from mangling the data
- Fixed line 96: Added do_exit=1 so that the error message doesn't get repeated while async read is being cancelled
- Removed synchronous read since it would constantly drop samples
- Added bounds check by changing strcmp to strncmp which stopped segfaults when reading arguments
