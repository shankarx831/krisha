// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

#ifndef AUDIOENGINEBASEAPO_H
#define AUDIOENGINEBASEAPO_H

#include "unknwn.h"
#include "audioapotypes.h"

class IAudioProcessingObject : public IUnknown {
public:
    virtual HRESULT Initialize(uint32_t cbDataSize, uint8_t* pbyData) = 0;
    virtual HRESULT IsInputFormatSupported(void* pOppositeFormat, void* pRequestedFormat, void** ppSupportedFormat) = 0;
    virtual HRESULT IsOutputFormatSupported(void* pOppositeFormat, void* pRequestedFormat, void** ppSupportedFormat) = 0;
};

class IAudioProcessingObjectConfiguration : public IUnknown {
public:
    virtual HRESULT LockForProcess(uint32_t u32NumInputConnections, APO_CONNECTION_DESCRIPTOR** ppInputConnections, uint32_t u32NumOutputConnections, APO_CONNECTION_DESCRIPTOR** ppOutputConnections) = 0;
    virtual HRESULT UnlockForProcess() = 0;
};

class IAudioProcessingObjectRT : public IUnknown {
public:
    virtual void APOProcess(uint32_t u32NumInputConnections, APO_CONNECTION_PROPERTY** ppInputConnections, uint32_t u32NumOutputConnections, APO_CONNECTION_PROPERTY** ppOutputConnections) = 0;
};

#endif // AUDIOENGINEBASEAPO_H
