/**
 * Chaos Emerging Advanced Build tools.
 * Viewer-side operations using the existing OpenSim-compatible selection APIs.
 */
#pragma once

#include "llfloater.h"
#include "lluuid.h"
#include "llsd.h"
#include "llvector3.h"
#include <vector>

class LLViewerObject;

class LLFloaterAdvancedBuild final : public LLFloater,
                                    public LLFloaterSingleton<LLFloaterAdvancedBuild>
{
    friend class LLUISingleton<LLFloaterAdvancedBuild, VisibilityPolicy<LLFloater> >;

private:
    LLFloaterAdvancedBuild(const LLSD&);
    bool postBuild() override;
    void onOpen() override;

    F32 spin(const char* name) const;
    void setSpin(const char* name, F32 value);
    std::vector<LLViewerObject*> editableRoots(bool copy_needed = false) const;
    void updateMeasurements();
    void updateLinks();
    void doMove();
    void doAlign();
    void doDistribute();
    void doRotate();
    void doArray();
    void doHUDZoom(bool zoom_in);
    void doCopyUV();
    void doPasteUV();
    void doSelectLink();
    void doSelectFace();
    void doSavePreset();
    void doLoadPreset();
    void doApplyShapePreset();

    static void onMove(void*);
    static void onAlign(void*);
    static void onDistribute(void*);
    static void onRotate(void*);
    static void onArray(void*);
    static void onZoomIn(void*);
    static void onZoomOut(void*);
    static void onCopyUV(void*);
    static void onPasteUV(void*);
    static void onLinksRefresh(void*);
    static void onLinkSelect(void*);
    static void onFaceSelect(void*);
    static void onMeasure(void*);
    static void onSavePreset(void*);
    static void onLoadPreset(void*);
    static void onApplyShapePreset(void*);

    struct UVSettings
    {
        F32 scale_s = 1.f, scale_t = 1.f;
        F32 offset_s = 0.f, offset_t = 0.f;
        F32 rotation = 0.f;
        U8 texgen = 0;
        bool valid = false;
    };

    UVSettings mUV;
    LLSD mSavedShape;
    LLVector3 mSavedShapeScale;
    bool mShapeValid = false;
    std::vector<LLUUID> mLinkIDs;
};
