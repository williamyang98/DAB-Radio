## Description
Contains code that is used in the digital decoding stage for DAB (digital audio broadcast).

## Folder layout
| Folder | Description |
| --- | --- |
| algorithms | Commonly used algorithms that are not specific to DAB |
| audio | Audio codecs used in DAB: AAC, MPEG-II (not supported yet) |
| constants | DAB constants that are used in decoding |
| database | A simple implementation of the DAB database for an ensemble |
| fic | Decoding for the fast information channel (FIC) |
| msc | Decoding for the main service channel (MSC) |
| pad | Decodes program associated data |
| mot | Reconstructs file entities from multimedia object transfers |

## Usage
All the code here can be reused in other projects without dependencies.

I.e. Implement your own DAB database, etc...

## Sources
The ETSI standards were heavily relied upon for developing this implementation of a DAB decoder.

| Document | Description |
| --- | --- |
| ETSI EN 300 401 | Description of how DAB works as a whole |
| ETSI EN 301 234 | Description of how multimedia object transfer |
| ETSI TS 101 756 | Tables of constants used by DAB in it's FIC packets |
| ETSI TS 102 563 | Description of audio super framing and AAC codec |
| ETSI TS 101 499 | Description of slideshow |
| ETSI TS 103 466 | Description of MPEG-II usage |

The design of welle.io was also used to help debug an undocumented part of the standard. This was discovered in the bitfield layout of FIG 0/17 - Programme type. This particular FIG (fast information group) contains additional language and closed caption information which the standard doesn't describe in ETSI EN 300 401 v2.1.1.