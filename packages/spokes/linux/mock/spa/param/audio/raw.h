#ifndef MOCK_SPA_RAW_H
#define MOCK_SPA_RAW_H

#ifdef __cplusplus
extern "C" {
#endif

enum spa_audio_format {
    SPA_AUDIO_FORMAT_UNKNOWN,
    SPA_AUDIO_FORMAT_ENCODED,
    SPA_AUDIO_FORMAT_S8,
    SPA_AUDIO_FORMAT_U8,
    SPA_AUDIO_FORMAT_S16,
    SPA_AUDIO_FORMAT_S24,
    SPA_AUDIO_FORMAT_S32,
    SPA_AUDIO_FORMAT_F32,
};

struct spa_audio_info_raw {
    enum spa_audio_format format;
    uint32_t flags;
    uint32_t rate;
    uint32_t channels;
    uint32_t position[64];
};

#ifdef __cplusplus
}
#endif

#endif // MOCK_SPA_RAW_H
