# Chaos Emerging Viewer

Chaos Emerging Viewer is a third-party OpenSim desktop viewer for Windows, developed from the Cool VL Viewer codebase and with modifications to its user interface, virtual-world functions, and compatibility. The project is not an official OpenSimulator or Linden Lab viewer.

## Source provenance and licences

This repository contains a source snapshot from the Chaos Emerging development tree, based on Cool VL Viewer. Source files retain their original copyright and licence notices. See `doc/source-license.txt`, `doc/GPLv2-license.txt` and `doc/FLOSS-exception.txt`; different components may have different terms. Retaining these notices is mandatory. Third-party assets must be used within their own licence terms.

The Chaos Emerging branding artwork is supplied separately for the project. Its presence does not change the licences of upstream source or third-party assets.

## Status

**Source snapshot only — not yet a reproducible public release.** This snapshot corresponds to the development tree used for the Windows Chaos Emerging Viewer line around Build 077 (1.32.4.121). It has **not** been validated as a clean build from a GitHub-hosted runner. Do not represent the locally compiled Build 077 installer as built from this repository.

The existing desktop build uses third-party prebuilt dependencies. This source snapshot deliberately excludes the local `bin/`, `lib/`, `include/`, `resources/`, Visual Studio output, development checkpoints, and bundled proprietary voice binaries. Consequently, the included build scripts will not run successfully from this Git checkout alone. Dependency acquisition, licence review and a clean build workflow are outstanding tasks.

## Building

See `doc/WindowsBuildHowto.txt` and the original `windows-build.bat`. The Windows development environment uses Visual Studio 2022. Required upstream dependencies must be obtained under their applicable terms before the code can be built. This is not a one-command GitHub Actions build yet.

## Voice dependency

The existing Chaos Emerging Windows package includes proprietary Vivox components (for example `vivoxsdk_x64.dll` and `SLVoice.exe`). These binaries are intentionally **not included** in this repository. Do not upload or redistribute third-party proprietary binaries without sufficient rights.

In particular, **the existing Vivox-enabled installer is not currently eligible for free SignPath Foundation signing merely because the source is published**. An eligible public release will require a licensing and dependency review, and may require a distinct fully open-source release configuration.

## Signing status

No SignPath application has been approved, and there is no trusted signing integration. See [Code signing policy](CODE_SIGNING_POLICY.md).

## Privacy

As an OpenSim viewer, Chaos Emerging connects to grid login and simulator services selected by users, and may access user-selected voice, media, and stream endpoints. The relevant third-party services are governed by their own policies. No claim of independent security or privacy audit is made here.

## Releases

The previous locally packaged Build 077 executable is not a release built from this repository. Future eligible releases must be reproducibly built from a reviewed tagged commit and signed only after SignPath approval.

## Project maintenance

Maintainer: [debbiephoenix1985-cloud](https://github.com/debbiephoenix1985-cloud)
