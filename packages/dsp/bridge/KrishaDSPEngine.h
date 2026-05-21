// Copyright (C) Radioform / Original Authors
// Modified by Shankar (2026) for the KRISHA Architecture. Renamed namespaces and variables.
// Licensed under the GNU GPLv3.

/**
 * @file KrishaDSPEngine.h
 * @brief Objective-C wrapper for Krisha DSP Engine
 *
 * This provides a clean, Swift-friendly API over the C DSP engine.
 */

#import <Foundation/Foundation.h>

NS_ASSUME_NONNULL_BEGIN

// ============================================================================
// Error Domain
// ============================================================================

extern NSErrorDomain const KrishaDSPErrorDomain;

typedef NS_ERROR_ENUM(KrishaDSPErrorDomain, KrishaDSPError) {
    KrishaDSPErrorNone = 0,
    KrishaDSPErrorNullPointer = 1,
    KrishaDSPErrorInvalidParameter = 2,
    KrishaDSPErrorOutOfMemory = 3,
    KrishaDSPErrorUnknown = 99
};

// ============================================================================
// Filter Types
// ============================================================================

typedef NS_ENUM(NSInteger, KrishaFilterType) {
    KrishaFilterTypePeak = 0,        // Parametric peak/dip
    KrishaFilterTypeLowShelf = 1,    // Low shelf
    KrishaFilterTypeHighShelf = 2,   // High shelf
    KrishaFilterTypeLowPass = 3,     // Low-pass
    KrishaFilterTypeHighPass = 4,    // High-pass
    KrishaFilterTypeNotch = 5,       // Notch filter
    KrishaFilterTypeBandPass = 6     // Band-pass
};

// ============================================================================
// Band Configuration
// ============================================================================

@interface KrishaBand : NSObject <NSCopying>

@property (nonatomic, assign) float frequencyHz;
@property (nonatomic, assign) float gainDb;
@property (nonatomic, assign) float qFactor;
@property (nonatomic, assign) KrishaFilterType filterType;
@property (nonatomic, assign) BOOL enabled;

- (instancetype)initWithFrequency:(float)frequency
                             gain:(float)gain
                          qFactor:(float)q
                       filterType:(KrishaFilterType)type;

@end

// ============================================================================
// Preset Configuration
// ============================================================================

@interface KrishaPreset : NSObject <NSCopying>

@property (nonatomic, copy) NSString *name;
@property (nonatomic, strong) NSArray<KrishaBand *> *bands;
@property (nonatomic, assign) float preampDb;
@property (nonatomic, assign) BOOL limiterEnabled;
@property (nonatomic, assign) float limiterThresholdDb;

/// Create a flat preset (transparent, no processing)
+ (instancetype)flatPreset;

/// Create a preset with custom bands
+ (instancetype)presetWithName:(NSString *)name bands:(NSArray<KrishaBand *> *)bands;

/// Validate preset parameters
- (BOOL)isValid;

@end

// ============================================================================
// Engine Statistics
// ============================================================================

@interface KrishaStats : NSObject

@property (nonatomic, assign, readonly) uint64_t framesProcessed;
@property (nonatomic, assign, readonly) uint32_t underrunCount;
@property (nonatomic, assign, readonly) float cpuLoadPercent;
@property (nonatomic, assign, readonly) BOOL bypassActive;
@property (nonatomic, assign, readonly) uint32_t sampleRate;

@end

// ============================================================================
// DSP Engine
// ============================================================================

@interface KrishaDSPEngine : NSObject

/// Initialize engine with sample rate
- (nullable instancetype)initWithSampleRate:(uint32_t)sampleRate error:(NSError **)error;

/// Current sample rate
@property (nonatomic, assign, readonly) uint32_t sampleRate;

/// Change sample rate (will reset filter state)
- (BOOL)setSampleRate:(uint32_t)sampleRate error:(NSError **)error;

/// Apply a preset to the engine
- (BOOL)applyPreset:(KrishaPreset *)preset error:(NSError **)error;

/// Get current preset
- (KrishaPreset *)currentPreset;

/// Process interleaved stereo audio (LRLRLR...)
/// @param inputBuffer Input audio samples
/// @param outputBuffer Output audio samples (can be same as input for in-place processing)
/// @param frameCount Number of stereo frames
- (void)processInterleaved:(const float *)inputBuffer
                    output:(float *)outputBuffer
                frameCount:(uint32_t)frameCount;

/// Process planar stereo audio (separate L and R buffers)
/// @param inputLeft Left channel input
/// @param inputRight Right channel input
/// @param outputLeft Left channel output
/// @param outputRight Right channel output
/// @param frameCount Number of frames per channel
- (void)processPlanarLeft:(const float *)inputLeft
                    right:(const float *)inputRight
               outputLeft:(float *)outputLeft
              outputRight:(float *)outputRight
               frameCount:(uint32_t)frameCount;

/// Update a single band's gain in realtime (safe to call from audio thread)
- (void)updateBandGain:(NSUInteger)bandIndex gainDb:(float)gainDb;

/// Update preamp gain in realtime (safe to call from audio thread)
- (void)updatePreampGain:(float)gainDb;

/// Enable/disable bypass (safe to call from audio thread)
@property (nonatomic, assign) BOOL bypass;

/// Reset all filter state (clears delay lines)
- (void)reset;

/// Get current statistics
- (KrishaStats *)statistics;

@end

NS_ASSUME_NONNULL_END
