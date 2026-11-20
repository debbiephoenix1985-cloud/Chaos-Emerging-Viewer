/**
 * Chaos Emerging: extended OpenSim build and edit operations.
 * Uses actual viewer selection/texture and object update APIs.
 */
#include "llviewerprecompiledheaders.h"
#include "llfloateradvancedbuild.h"

#include "llagent.h"
#include "llbutton.h"
#include "llcombobox.h"
#include "lldir.h"
#include "llfile.h"
#include "lllineeditor.h"
#include "llsdserialize.h"
#include "llselectmgr.h"
#include "llspinctrl.h"
#include "lltextbox.h"
#include "lltextureentry.h"
#include "llvolume.h"
#include "lluictrlfactory.h"
#include "llviewerobject.h"
#include "llviewerobjectlist.h"
#include <algorithm>
#include <cmath>
#include <sstream>

namespace
{
    // Include linked children in measurements, as the native QL align tool does.
    LLBBox buildBounds(LLViewerObject* obj)
    {
        LLBBox box(obj->getPositionAgent(), LLQuaternion(),
                   LLVector3::zero, LLVector3::zero);
        box.addBBoxAgent(obj->getBoundingBoxAgent());
        const LLViewerObject::const_child_list_t& children = obj->getChildren();
        for (LLViewerObject::const_child_list_t::const_iterator it = children.begin();
             it != children.end(); ++it)
        {
            if (*it) box.addBBoxAgent((*it)->getBoundingBoxAgent());
        }
        return box;
    }
    F32 edge(const LLBBox& box, S32 axis, S32 mode)
    {
        const F32 center = box.getCenterAgent().mV[axis];
        const F32 half = box.getExtentLocal().mV[axis] * 0.5f;
        return center + (mode - 1) * half;
    }
    struct MeasuredObject
    {
        LLViewerObject* object;
        LLBBox bounds;
    };
}

LLFloaterAdvancedBuild::LLFloaterAdvancedBuild(const LLSD&) : LLFloater("advanced_build")
{
    LLUICtrlFactory::getInstance()->buildFloater(this, "floater_advanced_build.xml");
}

bool LLFloaterAdvancedBuild::postBuild()
{
    getChild<LLButton>("adv_measure")->setClickedCallback(onMeasure, this);
    getChild<LLButton>("adv_move")->setClickedCallback(onMove, this);
    getChild<LLButton>("adv_align")->setClickedCallback(onAlign, this);
    getChild<LLButton>("adv_distribute")->setClickedCallback(onDistribute, this);
    getChild<LLButton>("adv_rotate")->setClickedCallback(onRotate, this);
    getChild<LLButton>("adv_array")->setClickedCallback(onArray, this);
    getChild<LLButton>("adv_hud_plus")->setClickedCallback(onZoomIn, this);
    getChild<LLButton>("adv_hud_minus")->setClickedCallback(onZoomOut, this);
    getChild<LLButton>("adv_uv_copy")->setClickedCallback(onCopyUV, this);
    getChild<LLButton>("adv_uv_paste")->setClickedCallback(onPasteUV, this);
    getChild<LLButton>("adv_refresh_links")->setClickedCallback(onLinksRefresh, this);
    getChild<LLButton>("adv_select_link")->setClickedCallback(onLinkSelect, this);
    getChild<LLButton>("adv_select_face")->setClickedCallback(onFaceSelect, this);
    getChild<LLButton>("adv_save_preset")->setClickedCallback(onSavePreset, this);
    getChild<LLButton>("adv_load_preset")->setClickedCallback(onLoadPreset, this);
    getChild<LLButton>("adv_apply_shape")->setClickedCallback(onApplyShapePreset, this);
    return true;
}

void LLFloaterAdvancedBuild::onOpen()
{
    updateMeasurements();
    updateLinks();
}

F32 LLFloaterAdvancedBuild::spin(const char* name) const
{
    return getChild<LLSpinCtrl>(name)->getValue().asReal();
}
void LLFloaterAdvancedBuild::setSpin(const char* name, F32 v)
{
    getChild<LLSpinCtrl>(name)->setValue(v);
}
std::vector<LLViewerObject*> LLFloaterAdvancedBuild::editableRoots(bool copy_needed) const
{
    std::vector<LLViewerObject*> objects;
    if (!gSelectMgr.getSelection() || gSelectMgr.getSelection()->isAttachment())
        return objects;
    for (LLObjectSelection::root_iterator it = gSelectMgr.getSelection()->root_begin(),
             end = gSelectMgr.getSelection()->root_end(); it != end; ++it)
    {
        LLSelectNode* node = *it;
        LLViewerObject* object = node ? node->getObject() : NULL;
        if (!object || object->isDead() || !object->permMove() ||
            object->isPermanentEnforced() || (copy_needed && !object->permCopy()))
        {
            objects.clear();
            return objects;
        }
        objects.push_back(object);
    }
    return objects;
}
void LLFloaterAdvancedBuild::updateMeasurements()
{
    std::ostringstream s;
    const std::vector<LLViewerObject*> objects = editableRoots();
    if (objects.empty())
    {
        s << "Select movable objects to measure.";
    }
    else
    {
        const LLBBox bbox = gSelectMgr.getBBoxOfSelection();
        const LLVector3 dims = bbox.getExtentLocal();
        s.setf(std::ios::fixed);
        s.precision(3);
        s << objects.size() << " roots | X " << dims.mV[VX]
          << "  Y " << dims.mV[VY] << "  Z " << dims.mV[VZ] << " m";
        if (objects.size() == 2)
        {
            const F32 dist = dist_vec(objects[0]->getPositionAgent(),
                                      objects[1]->getPositionAgent());
            s << " | centres " << dist << " m";
        }
    }
    getChild<LLTextBox>("adv_measure_result")->setText(s.str());
}

void LLFloaterAdvancedBuild::doMove()
{
    const std::vector<LLViewerObject*> objects = editableRoots();
    if (objects.empty()) return;
    const LLVector3 delta(spin("adv_move_x"), spin("adv_move_y"),
                          spin("adv_move_z"));
    if (delta.lengthSquared() > 65536.f) return; // refuse accidental huge edits
    for (size_t i = 0; i < objects.size(); ++i)
        objects[i]->setPositionLocal(objects[i]->getPositionAgent() + delta);
    gSelectMgr.sendMultipleUpdate(UPD_POSITION);
    gSelectMgr.updateSelectionCenter();
    updateMeasurements();
}
void LLFloaterAdvancedBuild::doAlign()
{
    const std::vector<LLViewerObject*> objects = editableRoots();
    if (objects.size() < 2) return;
    const S32 axis = llclamp(getChild<LLComboBox>("adv_align_axis")->getValue().asInteger(), 0, 2);
    const S32 mode = llclamp(getChild<LLComboBox>("adv_align_mode")->getValue().asInteger(), 0, 2);
    std::vector<LLBBox> boxes;
    F32 minimum = F32_MAX, maximum = -F32_MAX;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        boxes.push_back(buildBounds(objects[i]));
        minimum = llmin(minimum, edge(boxes.back(), axis, 0));
        maximum = llmax(maximum, edge(boxes.back(), axis, 2));
    }
    const F32 target = mode == 0 ? minimum :
                       mode == 2 ? maximum : (minimum + maximum) * 0.5f;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        LLVector3 offset = LLVector3::zero;
        offset.mV[axis] = target - edge(boxes[i], axis, mode);
        objects[i]->setPositionLocal(objects[i]->getPositionAgent() + offset);
    }
    gSelectMgr.sendMultipleUpdate(UPD_POSITION);
    gSelectMgr.updateSelectionCenter();
    updateMeasurements();
}
void LLFloaterAdvancedBuild::doDistribute()
{
    const std::vector<LLViewerObject*> objects = editableRoots();
    if (objects.size() < 3) return;
    const S32 axis = llclamp(getChild<LLComboBox>("adv_dist_axis")->getValue().asInteger(), 0, 2);
    std::vector<MeasuredObject> sorted;
    F32 total_length = 0.f;
    for (size_t i = 0; i < objects.size(); ++i)
    {
        const LLBBox bounds = buildBounds(objects[i]);
        sorted.push_back(MeasuredObject{ objects[i], bounds });
        total_length += bounds.getExtentLocal().mV[axis];
    }
    std::sort(sorted.begin(), sorted.end(),
              [axis](const MeasuredObject& a, const MeasuredObject& b)
              {
                  return edge(a.bounds, axis, 0) < edge(b.bounds, axis, 0);
              });
    const F32 start = edge(sorted.front().bounds, axis, 0);
    const F32 end = edge(sorted.back().bounds, axis, 2);
    const F32 gap = (end - start - total_length) / (sorted.size() - 1);
    F32 next = start;
    // Keep the extreme objects fixed; rearrange the objects between them.
    for (size_t i = 0; i < sorted.size(); ++i)
    {
        if (i > 0 && i + 1 < sorted.size())
        {
            LLVector3 offset = LLVector3::zero;
            offset.mV[axis] = next - edge(sorted[i].bounds, axis, 0);
            sorted[i].object->setPositionLocal(sorted[i].object->getPositionAgent() + offset);
        }
        next += sorted[i].bounds.getExtentLocal().mV[axis] + gap;
    }
    gSelectMgr.sendMultipleUpdate(UPD_POSITION);
    gSelectMgr.updateSelectionCenter();
    updateMeasurements();
}
void LLFloaterAdvancedBuild::doRotate()
{
    const std::vector<LLViewerObject*> objects = editableRoots();
    if (objects.empty()) return;
    const F32 x = spin("adv_rot_x") * DEG_TO_RAD;
    const F32 y = spin("adv_rot_y") * DEG_TO_RAD;
    const F32 z = spin("adv_rot_z") * DEG_TO_RAD;
    const LLQuaternion rotation =
        LLQuaternion(x, LLVector3(1.f, 0.f, 0.f)) *
        LLQuaternion(y, LLVector3(0.f, 1.f, 0.f)) *
        LLQuaternion(z, LLVector3(0.f, 0.f, 1.f));
    const LLVector3 centre = gSelectMgr.getBBoxOfSelection().getCenterAgent();
    for (size_t i = 0; i < objects.size(); ++i)
    {
        LLViewerObject* object = objects[i];
        const LLVector3 pos = object->getPositionAgent();
        object->setRotation(object->getRotationEdit() * rotation);
        if (objects.size() > 1)
            object->setPositionLocal(centre + (pos - centre) * rotation);
    }
    gSelectMgr.sendMultipleUpdate(UPD_ROTATION | UPD_POSITION);
    gSelectMgr.updateSelectionCenter();
    updateMeasurements();
}

void LLFloaterAdvancedBuild::doArray()
{
    const std::vector<LLViewerObject*> objects = editableRoots(true);
    if (objects.empty()) return;
    if (gSelectMgr.getSelection()->getSelectType() == SELECT_TYPE_HUD) return;
    const S32 nx = llclamp((S32)spin("adv_array_nx"), 1, 6);
    const S32 ny = llclamp((S32)spin("adv_array_ny"), 1, 6);
    const S32 nz = llclamp((S32)spin("adv_array_nz"), 1, 6);
    const LLVector3 step(spin("adv_array_dx"), spin("adv_array_dy"),
                         spin("adv_array_dz"));
    // Multiple duplicate messages go to the current simulator; cap the burst.
    if (nx * ny * nz > 64 || step.lengthSquared() > 65536.f) return;
    for (S32 x = 0; x < nx; ++x)
    for (S32 y = 0; y < ny; ++y)
    for (S32 z = 0; z < nz; ++z)
    {
        if (x == 0 && y == 0 && z == 0) continue;
        const LLVector3 offset(x * step.mV[VX], y * step.mV[VY],
                               z * step.mV[VZ]);
        gSelectMgr.selectDuplicate(offset, false);
    }
    getChild<LLTextBox>("adv_status")->setText("Array duplicate requests sent; check region prim capacity.");
}
void LLFloaterAdvancedBuild::doHUDZoom(bool zoom_in)
{
    if (!gSelectMgr.getSelection() ||
        gSelectMgr.getSelection()->getSelectType() != SELECT_TYPE_HUD)
    {
        getChild<LLTextBox>("adv_status")->setText("Select a HUD attachment first.");
        return;
    }
    F32 target, current;
    gAgent.getHUDZoom(target, current);
    const F32 factor = zoom_in ? 1.25f : 0.8f;
    target = llclamp(target * factor, 0.1f, 25.f);
    current = llclamp(current * factor, 0.1f, 25.f);
    gAgent.setHUDZoom(target, current);
}
void LLFloaterAdvancedBuild::doCopyUV()
{
    mUV.valid = false;
    if (!gSelectMgr.getSelection()) return;
    for (LLObjectSelection::iterator it = gSelectMgr.getSelection()->begin(),
         end = gSelectMgr.getSelection()->end(); it != end; ++it)
    {
        LLSelectNode* node = *it;
        if (!node || !node->getObject()) continue;
        const S32 index = node->getLastSelectedTE();
        if (index < 0 || index >= node->getObject()->getNumTEs()) continue;
        const LLTextureEntry* te = node->getObject()->getTE(index);
        if (!te) continue;
        mUV.scale_s = te->getScaleS();
        mUV.scale_t = te->getScaleT();
        mUV.offset_s = te->getOffsetS();
        mUV.offset_t = te->getOffsetT();
        mUV.rotation = te->getRotation();
        mUV.texgen = te->getTexGen();
        mUV.valid = true;
    }
    getChild<LLTextBox>("adv_status")->setText(mUV.valid ?
        "UV settings copied from selected face." : "Select a textured face first.");
}
void LLFloaterAdvancedBuild::doPasteUV()
{
    if (!mUV.valid || !gSelectMgr.getSelection() ||
        !gSelectMgr.selectGetModify()) return;
    std::vector<LLViewerObject*> changed;
    for (LLObjectSelection::iterator it = gSelectMgr.getSelection()->begin(),
         end = gSelectMgr.getSelection()->end(); it != end; ++it)
    {
        LLSelectNode* node = *it;
        LLViewerObject* object = node ? node->getObject() : NULL;
        if (!object || !object->permModify()) continue;
        for (S32 face = 0; face < object->getNumTEs(); ++face)
        {
            if (!node->isTESelected(face)) continue;
            object->setTEScale(face, mUV.scale_s, mUV.scale_t);
            object->setTEOffset(face, mUV.offset_s, mUV.offset_t);
            object->setTERotation(face, mUV.rotation);
            object->setTETexGen(face, mUV.texgen);
        }
        changed.push_back(object);
    }
    for (size_t i = 0; i < changed.size(); ++i)
        changed[i]->sendTEUpdate();
    getChild<LLTextBox>("adv_status")->setText("UV settings applied to selected faces.");
}
void LLFloaterAdvancedBuild::updateLinks()
{
    mLinkIDs.clear();
    LLComboBox* combo = getChild<LLComboBox>("adv_links");
    combo->removeall();
    if (!gSelectMgr.getSelection()) return;
    LLViewerObject* object = gSelectMgr.getSelection()->getFirstRootObject(true);
    if (!object) return;
    if (object->getParent() && !object->getParent()->isAvatar())
        object = static_cast<LLViewerObject*>(object->getParent());

    const std::string filter = getChild<LLLineEditor>("adv_link_search")->getText();
    std::vector<LLViewerObject*> links;
    links.push_back(object);
    const LLViewerObject::const_child_list_t& children = object->getChildren();
    for (LLViewerObject::const_child_list_t::const_iterator it = children.begin();
         it != children.end(); ++it)
        if (*it) links.push_back(*it);

    for (size_t i = 0; i < links.size(); ++i)
    {
        LLViewerObject* link = links[i];
        std::ostringstream label;
        label << (i + 1) << ": " << (i == 0 ? "Root" : "Link") << " ("
              << link->getID().asString().substr(0, 8) << ") "
              << link->getNumTEs() << " faces";
        if (!filter.empty() && label.str().find(filter) == std::string::npos)
            continue;
        mLinkIDs.push_back(link->getID());
        combo->addSimpleElement(label.str(), ADD_BOTTOM,
                                 LLSD((S32)mLinkIDs.size() - 1));
    }
    if (!mLinkIDs.empty()) combo->setCurrentByIndex(0);
}
void LLFloaterAdvancedBuild::doSelectLink()
{
    const S32 index = getChild<LLComboBox>("adv_links")->getValue().asInteger();
    if (index < 0 || index >= (S32)mLinkIDs.size()) return;
    LLViewerObject* object = gObjectList.findObject(mLinkIDs[index]);
    if (!object || object->isDead()) return;
    gSelectMgr.deselectAll();
    gSelectMgr.selectObjectOnly(object);
    updateMeasurements();
}

void LLFloaterAdvancedBuild::doSelectFace()
{
    const S32 index = getChild<LLComboBox>("adv_links")->getValue().asInteger();
    if (index < 0 || index >= (S32)mLinkIDs.size()) return;
    LLViewerObject* object = gObjectList.findObject(mLinkIDs[index]);
    if (!object || object->isDead()) return;
    const S32 face = (S32)spin("adv_face_index");
    if (face < 0 || face >= object->getNumTEs())
    {
        getChild<LLTextBox>("adv_status")->setText("Face number exceeds available faces.");
        return;
    }
    gSelectMgr.deselectAll();
    gSelectMgr.selectObjectOnly(object, face);
    getChild<LLTextBox>("adv_status")->setText("Selected one face for texture editing.");
}

void LLFloaterAdvancedBuild::doSavePreset()
{
    static const char* fields[] =
    {
        "adv_move_x", "adv_move_y", "adv_move_z",
        "adv_rot_x", "adv_rot_y", "adv_rot_z",
        "adv_array_nx", "adv_array_ny", "adv_array_nz",
        "adv_array_dx", "adv_array_dy", "adv_array_dz"
    };
    LLSD settings = LLSD::emptyMap();
    for (size_t i = 0; i < LL_ARRAY_SIZE(fields); ++i)
        settings[fields[i]] = spin(fields[i]);
    settings["align_axis"] = getChild<LLComboBox>("adv_align_axis")->getValue();
    settings["align_mode"] = getChild<LLComboBox>("adv_align_mode")->getValue();
    settings["dist_axis"] = getChild<LLComboBox>("adv_dist_axis")->getValue();
    // Capture the current parametric prim shape, if there is exactly one.
    // Sculpted and mesh objects are deliberately excluded.
    const std::vector<LLViewerObject*> roots = editableRoots();
    if (roots.size() == 1 && roots[0]->getPCode() == LL_PCODE_VOLUME &&
        !roots[0]->isSculpted() && !roots[0]->isMesh() && roots[0]->getVolume())
    {
        settings["prim_shape"] = roots[0]->getVolume()->getParams().asLLSD();
        const LLVector3& size = roots[0]->getScale();
        settings["prim_size_x"] = size.mV[VX];
        settings["prim_size_y"] = size.mV[VY];
        settings["prim_size_z"] = size.mV[VZ];
    }
    if (mUV.valid)
    {
        settings["uv_s"] = mUV.scale_s;
        settings["uv_t"] = mUV.scale_t;
        settings["uv_os"] = mUV.offset_s;
        settings["uv_ot"] = mUV.offset_t;
        settings["uv_rot"] = mUV.rotation;
        settings["uv_gen"] = (S32)mUV.texgen;
    }
    std::string name = getChild<LLLineEditor>("adv_preset_name")->getText();
    if (name.empty()) name = "Default";
    if (name.size() > 40) name.resize(40);
    const std::string path = gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT,
                                                 "chaos_build_preset.xml");
    LLSD saved = LLSD::emptyMap();
    llifstream previous(path.c_str());
    if (previous.is_open())
    {
        LLSDSerialize::fromXML(saved, previous);
        previous.close();
    }
    if (!saved.isMap()) saved = LLSD::emptyMap();
    if (!saved.has("presets") && saved.has("adv_move_x"))
    {
        LLSD legacy = saved;
        saved = LLSD::emptyMap();
        saved["presets"]["Default"] = legacy;
    }
    saved["presets"][name] = settings;
    llofstream file(path.c_str());
    if (!file.is_open() || !LLSDSerialize::toPrettyXML(saved, file))
        getChild<LLTextBox>("adv_status")->setText("Could not save build preset.");
    else
        getChild<LLTextBox>("adv_status")->setText("Named build preset saved.");
}
void LLFloaterAdvancedBuild::doLoadPreset()
{
    const std::string path = gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT,
                                                 "chaos_build_preset.xml");
    llifstream file(path.c_str());
    if (!file.is_open())
    {
        getChild<LLTextBox>("adv_status")->setText("No saved tool preset for this account.");
        return;
    }
    LLSD settings;
    if (LLSDSerialize::fromXML(settings, file) <= 0 || !settings.isMap())
        return;
    if (settings.has("presets"))
    {
        std::string name = getChild<LLLineEditor>("adv_preset_name")->getText();
        if (name.empty()) name = "Default";
        if (!settings["presets"].has(name))
        {
            getChild<LLTextBox>("adv_status")->setText("That preset name was not found.");
            return;
        }
        settings = settings["presets"][name];
    }
    static const char* fields[] =
    {
        "adv_move_x", "adv_move_y", "adv_move_z",
        "adv_rot_x", "adv_rot_y", "adv_rot_z",
        "adv_array_nx", "adv_array_ny", "adv_array_nz",
        "adv_array_dx", "adv_array_dy", "adv_array_dz"
    };
    for (size_t i = 0; i < LL_ARRAY_SIZE(fields); ++i)
        if (settings.has(fields[i])) setSpin(fields[i], settings[fields[i]].asReal());
    if (settings.has("align_axis"))
        getChild<LLComboBox>("adv_align_axis")->setValue(settings["align_axis"]);
    if (settings.has("align_mode"))
        getChild<LLComboBox>("adv_align_mode")->setValue(settings["align_mode"]);
    if (settings.has("dist_axis"))
        getChild<LLComboBox>("adv_dist_axis")->setValue(settings["dist_axis"]);
    if (settings.has("uv_s") && settings.has("uv_t"))
    {
        mUV.scale_s = settings["uv_s"].asReal();
        mUV.scale_t = settings["uv_t"].asReal();
        mUV.offset_s = settings["uv_os"].asReal();
        mUV.offset_t = settings["uv_ot"].asReal();
        mUV.rotation = settings["uv_rot"].asReal();
        mUV.texgen = (U8)settings["uv_gen"].asInteger();
        mUV.valid = true;
    }
    mShapeValid = false;
    if (settings.has("prim_shape") && settings.has("prim_size_x") &&
        settings.has("prim_size_y") && settings.has("prim_size_z"))
    {
        mSavedShape = settings["prim_shape"];
        mSavedShapeScale.set(settings["prim_size_x"].asReal(),
                             settings["prim_size_y"].asReal(),
                             settings["prim_size_z"].asReal());
        mShapeValid = true;
    }
    getChild<LLTextBox>("adv_status")->setText("Preset loaded. Apply prim shape separately.");
}

void LLFloaterAdvancedBuild::doApplyShapePreset()
{
    const std::vector<LLViewerObject*> roots = editableRoots();
    if (!mShapeValid || roots.size() != 1) return;
    LLViewerObject* object = roots[0];
    if (object->getPCode() != LL_PCODE_VOLUME || !object->permModify() ||
        object->isSculpted() || object->isMesh() || !object->getVolume())
    {
        getChild<LLTextBox>("adv_status")->setText("Shape preset supports editable standard prims only.");
        return;
    }
    LLVolumeParams params;
    LLSD shape_data = mSavedShape;
    if (!params.fromLLSD(shape_data)) return;
    object->updateVolume(params); // Native ObjectShape update to simulator.
    object->setScale(mSavedShapeScale, true);
    gSelectMgr.sendMultipleUpdate(UPD_SCALE);
    getChild<LLTextBox>("adv_status")->setText("Saved parametric prim shape and size applied.");
}

// Keep callbacks free of UI-side command strings or implicit server actions.
void LLFloaterAdvancedBuild::onMove(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doMove(); }
void LLFloaterAdvancedBuild::onAlign(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doAlign(); }
void LLFloaterAdvancedBuild::onDistribute(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doDistribute(); }
void LLFloaterAdvancedBuild::onRotate(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doRotate(); }
void LLFloaterAdvancedBuild::onArray(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doArray(); }
void LLFloaterAdvancedBuild::onZoomIn(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doHUDZoom(true); }
void LLFloaterAdvancedBuild::onZoomOut(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doHUDZoom(false); }
void LLFloaterAdvancedBuild::onCopyUV(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doCopyUV(); }
void LLFloaterAdvancedBuild::onPasteUV(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doPasteUV(); }
void LLFloaterAdvancedBuild::onLinksRefresh(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->updateLinks(); }
void LLFloaterAdvancedBuild::onLinkSelect(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doSelectLink(); }
void LLFloaterAdvancedBuild::onFaceSelect(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doSelectFace(); }
void LLFloaterAdvancedBuild::onMeasure(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->updateMeasurements(); }
void LLFloaterAdvancedBuild::onSavePreset(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doSavePreset(); }
void LLFloaterAdvancedBuild::onLoadPreset(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doLoadPreset(); }
void LLFloaterAdvancedBuild::onApplyShapePreset(void* p) { static_cast<LLFloaterAdvancedBuild*>(p)->doApplyShapePreset(); }
