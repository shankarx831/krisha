/**
 * @file RadioformAPO.cpp
 * @brief Windows Audio Processing Object (sAPO) spoke implementation
 */

#include <unknwn.h>
#include <audioenginebaseapo.h>
#include <audioapotypes.h>
#include <string>
#include <atomic>
#include <cstring>
#include <new>

#include "radioform_dsp.h"

// Define a unique GUID for RadioformAPO class
// {D8A9F63C-311D-4952-B35E-2BC90A093D87}
static const GUID CLSID_RadioformAPO = {
    0xd8a9f63c, 0x311d, 0x4952, { 0xb3, 0x5e, 0x2b, 0xc9, 0x0a, 0x09, 0x3d, 0x87 }
};

// IIDs for the APO interfaces
static const GUID IID_IAudioProcessingObject = {
    0xFD27FF0A, 0x5EA0, 0x4F5C, { 0xB2, 0xD4, 0x8C, 0xA9, 0xD2, 0x5D, 0xCA, 0xE7 } // Mock IID
};

class RadioformAPO : public IAudioProcessingObject,
                     public IAudioProcessingObjectConfiguration,
                     public IAudioProcessingObjectRT {
private:
    std::atomic<uint32_t> m_u32RefCount;
    radioform_dsp_engine_t* m_pEngine;
    uint32_t m_u32SampleRate;
    uint32_t m_u32Channels;
    bool m_bIsLocked;

public:
    RadioformAPO() 
        : m_u32RefCount(1), 
          m_pEngine(nullptr), 
          m_u32SampleRate(48000), 
          m_u32Channels(2), 
          m_bIsLocked(false) {}

    virtual ~RadioformAPO() {
        if (m_pEngine) {
            radioform_dsp_destroy(m_pEngine);
        }
    }

    // ============================================================================
    // IUnknown
    // ============================================================================
    STDMETHOD(QueryInterface)(REFIID riid, void** ppvObject) override {
        if (!ppvObject) return E_POINTER;
        *ppvObject = nullptr;

        // Since COM uses multiple inheritance, we cast correctly to avoid pointer offset issues
        if (IsEqualGUID(riid, CLSID_RadioformAPO)) {
            *ppvObject = static_cast<IUnknown*>(static_cast<IAudioProcessingObject*>(this));
        } else if (IsEqualGUID(riid, IID_IAudioProcessingObject)) {
            *ppvObject = static_cast<IAudioProcessingObject*>(this);
        } else {
            return E_NOINTERFACE;
        }

        AddRef();
        return S_OK;
    }

    STDMETHOD_(uint32_t, AddRef)() override {
        return ++m_u32RefCount;
    }

    STDMETHOD_(uint32_t, Release)() override {
        uint32_t uRef = --m_u32RefCount;
        if (uRef == 0) {
            delete this;
        }
        return uRef;
    }

    // ============================================================================
    // IAudioProcessingObject
    // ============================================================================
    STDMETHOD(Initialize)(uint32_t cbDataSize, uint8_t* pbyData) override {
        // Intentionally simple initialization
        return S_OK;
    }

    STDMETHOD(IsInputFormatSupported)(void* pOppositeFormat, void* pRequestedFormat, void** ppSupportedFormat) override {
        if (!pRequestedFormat) return E_POINTER;
        WAVEFORMATEX* pReq = static_cast<WAVEFORMATEX*>(pRequestedFormat);
        
        // We only support IEEE Float format for best quality and direct processing
        if (pReq->wFormatTag == 3 /* WAVE_FORMAT_IEEE_FLOAT */ || 
            (pReq->wFormatTag == 0xFFFE /* WAVE_FORMAT_EXTENSIBLE */ && 
             pReq->wBitsPerSample == 32)) {
            return S_OK;
        }
        
        return E_INVALIDARG;
    }

    STDMETHOD(IsOutputFormatSupported)(void* pOppositeFormat, void* pRequestedFormat, void** ppSupportedFormat) override {
        return IsInputFormatSupported(pOppositeFormat, pRequestedFormat, ppSupportedFormat);
    }

    // ============================================================================
    // IAudioProcessingObjectConfiguration
    // ============================================================================
    STDMETHOD(LockForProcess)(
        uint32_t u32NumInputConnections, 
        APO_CONNECTION_DESCRIPTOR** ppInputConnections, 
        uint32_t u32NumOutputConnections, 
        APO_CONNECTION_DESCRIPTOR** ppOutputConnections
    ) override {
        if (m_bIsLocked) return E_FAIL;
        if (u32NumInputConnections < 1 || u32NumOutputConnections < 1) return E_INVALIDARG;
        if (!ppInputConnections || !ppOutputConnections || !ppInputConnections[0]) return E_POINTER;

        WAVEFORMATEX* pFormat = static_cast<WAVEFORMATEX*>(ppInputConnections[0]->pFormat);
        if (!pFormat) return E_INVALIDARG;

        m_u32SampleRate = pFormat->nSamplesPerSec;
        m_u32Channels = pFormat->nChannels;

        // Clean up previous engine
        if (m_pEngine) {
            radioform_dsp_destroy(m_pEngine);
            m_pEngine = nullptr;
        }

        // Initialize cross-platform C++ DSP engine for the negotiated sample rate
        m_pEngine = radioform_dsp_create(m_u32SampleRate);
        if (!m_pEngine) return E_FAIL;

        // Load a standard flat preset into the engine
        radioform_preset_t preset;
        radioform_dsp_preset_init_flat(&preset);
        radioform_dsp_apply_preset(m_pEngine, &preset);
        radioform_dsp_set_bypass(m_pEngine, false);

        m_bIsLocked = true;
        return S_OK;
    }

    STDMETHOD(UnlockForProcess)() override {
        if (!m_bIsLocked) return E_FAIL;

        // Destroy the DSP engine context when unlocking
        if (m_pEngine) {
            radioform_dsp_destroy(m_pEngine);
            m_pEngine = nullptr;
        }

        m_bIsLocked = false;
        return S_OK;
    }

    // ============================================================================
    // IAudioProcessingObjectRT
    // ============================================================================
    STDMETHOD_(void, APOProcess)(
        uint32_t u32NumInputConnections, 
        APO_CONNECTION_PROPERTY** ppInputConnections, 
        uint32_t u32NumOutputConnections, 
        APO_CONNECTION_PROPERTY** ppOutputConnections
    ) override {
        if (!m_bIsLocked || u32NumInputConnections < 1 || u32NumOutputConnections < 1) return;
        if (!ppInputConnections || !ppOutputConnections) return;

        APO_CONNECTION_PROPERTY* pIn = ppInputConnections[0];
        APO_CONNECTION_PROPERTY* pOut = ppOutputConnections[0];

        if (!pIn || !pOut || !pIn->pBuffer || !pOut->pBuffer) return;

        uint32_t u32FrameCount = pIn->u32ValidFrameCount;
        if (u32FrameCount == 0) return;

        // Realtime processing using our high-performance frozen C++ DSP Core
        if (m_pEngine && !radioform_dsp_get_bypass(m_pEngine)) {
            radioform_dsp_process_interleaved(m_pEngine, pIn->pBuffer, pOut->pBuffer, u32FrameCount);
        } else {
            // Passthrough bypass
            std::memcpy(pOut->pBuffer, pIn->pBuffer, u32FrameCount * m_u32Channels * sizeof(float));
        }

        pOut->u32ValidFrameCount = u32FrameCount;
        pOut->u32BufferFlags = pIn->u32BufferFlags;
    }
};

// COM class factory entry point
extern "C" __attribute__((visibility("default"))) HRESULT CreateRadioformAPOInstance(void** ppvObject) {
    if (!ppvObject) return E_POINTER;
    RadioformAPO* pAPO = new (std::nothrow) RadioformAPO();
    if (!pAPO) return E_FAIL;
    *ppvObject = static_cast<IAudioProcessingObject*>(pAPO);
    return S_OK;
}
