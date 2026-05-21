# `KrishaApp`

Menu bar app for Krisha on macOS. Built with SwiftUI + AppKit.

Last reviewed: March 1, 2026.

## Purpose

`KrishaApp` is the control plane for Krisha:

- Presents onboarding and settings UI.
- Manages EQ and preset state.
- Starts `KrishaHost` when needed.
- Coordinates driver install/update prompts.

It does not run real-time DSP processing.

## Key Capabilities

- 10-band EQ controls with per-band frequency, Q, and filter type.
- Bundled presets plus user presets persisted as JSON.
- First-run onboarding and driver installation flow.
- Sparkle-powered update checks.
- Menu bar-only interaction model.

## Prerequisites

- macOS 13 or newer.
- Xcode command line tools (`xcode-select --install`).
- Built host binary at one of the expected host paths (recommended: `packages/host/.build/release/KrishaHost`).
- Driver bundle available for install/update flows (`KrishaDriver.driver` from `packages/driver`).

## Build

```bash
cd apps/mac/KrishaApp
swift build
```

Release build:

```bash
cd apps/mac/KrishaApp
swift build -c release
```

## Run (Development)

Recommended flow from repo root:

```bash
cd packages/host
swift build -c release

cd ../../apps/mac/KrishaApp
swift run
```

If host discovery fails, the app prompts with:

```bash
cd packages/host && swift build -c release
```

## Runtime Behavior

- On first launch, onboarding is shown until completed.
- On normal launch after onboarding, the app runs as a menu bar accessory app.
- The app launches `KrishaHost` if it is not already running.
- On app termination, cleanup runs and host processes are terminated.

## Data and State Paths

- IPC preset file: `~/Library/Application Support/Krisha/preset.json`
- User presets directory: `~/Library/Application Support/Krisha/Presets/`
- App log: `~/Library/Logs/Krisha/app.log`
- Driver install target: `/Library/Audio/Plug-Ins/HAL/KrishaDriver.driver`

## Developer Flags and Environment

- `--reset-onboarding`: clears onboarding state before app startup.
- `KRISHA_ROOT`: optional root path used during host binary discovery.
- `ENABLE_LIQUID_GLASS=1`: forces `LIQUID_GLASS_AVAILABLE` compile define in `Package.swift`.

## Test Utility

`test-presets.sh` cycles bundled presets by writing each one to:

`~/Library/Application Support/Krisha/preset.json`

Run:

```bash
cd apps/mac/KrishaApp
./test-presets.sh
```

## Troubleshooting

- Host not found:
  - Build host in `packages/host` and relaunch app.
- Driver install/update fails:
  - Ensure the driver bundle exists and grant admin privileges when prompted.
- Presets not loading:
  - Verify bundled JSON files exist under `Sources/Resources/Presets`.
  - Check user preset JSON validity in `~/Library/Application Support/Krisha/Presets/`.
