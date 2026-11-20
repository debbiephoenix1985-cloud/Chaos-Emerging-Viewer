/**
 * @file llappearancelistener.h
 * Chaos Emerging Build 184.
 *
 * Port of the five Firestorm 7.2.5 LLAppearance LEAP operations.
 * Firestorm source: indra/newview/llappearancelistener.h
 * Firestorm branch: Firestorm_7.2.5 (FirestormViewer/phoenix-firestorm)
 * Original Copyright (C) 2024 Linden Research, Inc., LGPL v2.1.
 * Calls into Chaos Emerging's existing outfit machinery instead of
 * introducing incompatible v7 appearance-manager implementations.
 */
#pragma once

#include "lleventdispatcher.h"  // LLEventAPI in the Chaos/Cool VL codebase

class LLAppearanceListener final : public LLEventAPI
{
public:
    LLAppearanceListener();

private:
    void wearOutfit(const LLSD& data);
    void wearItems(const LLSD& data);
    void detachItems(const LLSD& data);
    void getOutfitsList(const LLSD& data);
    void getOutfitItems(const LLSD& data);
};

// Registers one viewer-side LLAppearance event pump, once at startup.
void initAppearanceLeapAPI();
