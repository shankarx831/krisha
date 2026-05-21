// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

#ifndef AUDIOAPOTYPES_H
#define AUDIOAPOTYPES_H

#include <stdint.h>
#include "unknwn.h"

typedef struct {
    uint16_t wFormatTag;
    uint16_t nChannels;
    uint32_t nSamplesPerSec;
    uint32_t nAvgBytesPerSec;
    uint16_t nBlockAlign;
    uint16_t wBitsPerSample;
    uint16_t cbSize;
} WAVEFORMATEX;

typedef struct {
    WAVEFORMATEX Format;
    union {
        uint16_t wValidBitsPerSample;
        uint16_t wSamplesPerBlock;
        uint16_t wReserved;
    } Samples;
    uint32_t dwChannelMask;
    GUID SubFormat;
} WAVEFORMATEXTENSIBLE;

typedef enum {
    APO_CONNECTION_BUFFER_TYPE_DATA = 0,
    APO_CONNECTION_BUFFER_TYPE_SILENT,
    APO_CONNECTION_BUFFER_TYPE_VOID
} APO_CONNECTION_BUFFER_TYPE;

typedef struct {
    float* pBuffer;
    uint32_t u32ValidFrameCount;
    APO_CONNECTION_BUFFER_TYPE u32BufferFlags;
    uint32_t u32Signature;
} APO_CONNECTION_PROPERTY;

typedef struct {
    void* pFormat; // WAVEFORMATEX*
    uint32_t u32MaxFrameCount;
} APO_CONNECTION_DESCRIPTOR;

#endif // AUDIOAPOTYPES_H
