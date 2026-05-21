// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

#ifndef UNKNWN_H
#define UNKNWN_H

#include <stdint.h>

typedef int32_t HRESULT;
#define S_OK ((HRESULT)0L)
#define E_POINTER ((HRESULT)0x80004003L)
#define E_INVALIDARG ((HRESULT)0x80070057L)
#define E_NOINTERFACE ((HRESULT)0x80004002L)
#define E_FAIL ((HRESULT)0x80004005L)

#define SUCCEEDED(hr) (((HRESULT)(hr)) >= 0)
#define FAILED(hr) (((HRESULT)(hr)) < 0)

#define STDMETHOD(method) virtual HRESULT method
#define STDMETHOD_(type, method) virtual type method
#define STDMETHODIMP HRESULT
#define STDMETHODIMP_(type) type

typedef struct {
    uint32_t Data1;
    uint16_t Data2;
    uint16_t Data3;
    uint8_t  Data4[8];
} GUID;

typedef const GUID& REFIID;
typedef const GUID& REFCLSID;

#include <string.h>

inline bool IsEqualGUID(REFIID rguid1, REFIID rguid2) {
    return memcmp(&rguid1, &rguid2, sizeof(GUID)) == 0;
}

class IUnknown {
public:
    virtual HRESULT QueryInterface(REFIID riid, void** ppvObject) = 0;
    virtual uint32_t AddRef() = 0;
    virtual uint32_t Release() = 0;
};

#endif // UNKNWN_H
