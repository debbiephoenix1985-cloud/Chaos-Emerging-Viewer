/**
 * @file fsfloaterposer.cpp
 * @brief View Model for posing your (and other) avatar(s).
 *
 * $LicenseInfo:firstyear=2024&license=viewerlgpl$
 * Phoenix Firestorm Viewer Source Code
 * Copyright (c) 2024 Angeldark Raymaker @ Second Life
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 *
 * Linden Research, Inc., 945 Battery Street, San Francisco, CA  94111  USA
 * $/LicenseInfo$
 */

#include "fsfloaterposer.h"
#include "hbfileselector.h"
#include "llbvhloader.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include "llmatrix3.h"
#include <ctime>
#include <chrono>
#include <functional>
#include <algorithm>
#include "lltextbox.h"
#include "fsposeranimator.h"
#include "fsvirtualtrackpad.h"
#include "fsposer_legacy_bindings.h"
#include "fsposer_legacy_menu_entries.h"
#include "llmenugl.h"
#include "llviewermenu.h"
#include "lluictrlfactory.h"
#include "llagent.h"
#include "llappviewer.h"
#include "llavatarnamecache.h"
#include "llcheckboxctrl.h"
#include "llcommonutils.h"
#include "llvoavatarpuppet.h"
#include "lldiriterator.h"
#include "llloadingindicator.h"
#include "llmutelist.h"
#include "llnotifications.h"
#include "llscrolllistctrl.h"
#include "llsdserialize.h"
#include "llsliderctrl.h"
#include "llstring.h"
#include "lltabcontainer.h"
#include "lltoolcomp.h"
#include "llviewercontrol.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llwindow.h"
#include "llcolor4.h"
#include "llviewerobjectlist.h"
#include "lleditmenuhandler.h"
#include "fsposerpathcompat.h"
#if LL_WINDOWS
#include <shellapi.h>
#pragma comment(lib, "Shell32.lib")
#endif
#include "llinventorymodel.h"
#include "llfloaterinventory.h"
#include "llpanelinventory.h"
#include "llfolderview.h"
#include "llviewerinventory.h"
#include "llkeyframemotion.h"
#include "lldatapacker.h"

namespace
{
constexpr char             POSE_INTERNAL_FORMAT_FILE_MASK[]    = "*.xml";
constexpr char             POSE_INTERNAL_FORMAT_FILE_EXT[]     = ".xml";
constexpr char             POSE_EXTERNAL_FORMAT_FILE_EXT[]     = ".bvh";
constexpr char             POSE_SAVE_SUBDIRECTORY[]            = "poses";
constexpr std::string_view POSE_PRESETS_HANDS_SUBDIRECTORY     = "hand_presets";
constexpr char             XML_LIST_HEADER_STRING_PREFIX[]     = "header_";
constexpr char             XML_LIST_TITLE_STRING_PREFIX[]      = "title_";
constexpr char             XML_JOINT_TRANSFORM_STRING_PREFIX[] = "joint_transform_";
constexpr char             XML_JOINT_FRAME_TRANSFORM_PREFIX[]  = "joint_frame_";
constexpr char             XML_JOINT_DELTAROT_STRING_PREFIX[]  = "joint_delta_rotate_";
constexpr char             BVH_JOINT_TRANSFORM_STRING_PREFIX[] = "bvh_joint_transform_";
constexpr std::string_view POSER_TRACKPAD_SENSITIVITY_SAVE_KEY = "FSPoserTrackpadSensitivity";
constexpr std::string_view POSER_STOPPOSINGWHENCLOSED_SAVE_KEY = "FSPoserStopPosingWhenClosed";
constexpr std::string_view POSER_SAVEEXTERNALFORMAT_SAVE_KEY   = "FSPoserSaveExternalFileAlso";
constexpr std::string_view POSER_SAVECONFIRMREQUIRED_SAVE_KEY  = "FSPoserOnSaveConfirmOverwrite";
constexpr std::string_view POSER_UNLOCKPELVISINBVH_SAVE_KEY    = "FSPoserPelvisUnlockedForBvhSave";
constexpr std::string_view POSER_SHOWBONEHIGHLIGHTS_SAVE_KEY   = "FSManipShowJointMarkers";
constexpr char             ICON_SAVE_OK[]                      = "icon_rotation_is_own_work";
constexpr char             ICON_SAVE_FAILED[]                  = "icon_save_failed_button";

}  // namespace

/// <summary>
/// The amount of deflection 'one unit' on the trackpad translates to in radians.
/// The trackpad ordinarily has a range of +1..-1; multiplied by PI, gives PI to -PI, or all 360 degrees of deflection.
/// </summary>
constexpr F32 NormalTrackpadRangeInRads = F_PI;

static bool validatePoserFilename(const LLWString& filename)
{
    for (llwchar ch : filename)
        if (ch < 32 || ch > 126 || ch == '|')
            return false;
    return true;
}

 // Independent editable copy. Never change the cache or original asset.
class FSFloaterPoser::InventoryMotion : public LLKeyframeMotion
{
public:
    InventoryMotion(LLVOAvatar* avatar, const JointMotionList& src)
    : LLKeyframeMotion(LLUUID::null), mOwned(new JointMotionList)
    {
        mCharacter = avatar;
        auto& dst = *mOwned;
        dst.mDuration = src.mDuration;
        dst.mLoopInPoint = src.mLoopInPoint;
        dst.mLoopOutPoint = src.mLoopOutPoint;
        dst.mEaseInDuration = src.mEaseInDuration;
        dst.mEaseOutDuration = src.mEaseOutDuration;
        dst.mBasePriority = src.mBasePriority;
        dst.mMaxPriority = src.mMaxPriority;
        dst.mHandPose = src.mHandPose;
        dst.mLoop = src.mLoop;
        dst.mEmoteID = src.mEmoteID;
        dst.mEmoteName = src.mEmoteName;
        dst.mPelvisBBox = src.mPelvisBBox;
        for (const auto* joint : src.mJointMotionArray)
        {
            if (!joint) continue;
            auto* copy = new JointMotion(*joint);
            copy->mRotationCurve.mNumKeys = (S32)copy->mRotationCurve.mKeys.size();
            copy->mPositionCurve.mNumKeys = (S32)copy->mPositionCurve.mKeys.size();
            copy->mScaleCurve.mNumKeys = (S32)copy->mScaleCurve.mKeys.size();
            dst.mJointMotionArray.push_back(copy);
        }
        for (const auto* c : src.mConstraints)
        {
            if (!c) continue;
            auto* copy = new JointConstraintSharedData(*c);
            // Only the serialized constraint metadata is used by the editor.
            // Do not share the cache's owned runtime joint index array.
            copy->mJointStateIndices = nullptr;
            dst.mConstraints.push_back(copy);
        }
        mJointMotionList = mOwned.get();
    }
    ~InventoryMotion() override { mJointMotionList = nullptr; }
    JointMotionList& data() { return *mOwned; }
    JointMotion* track(const std::string& name)
    {
        for (auto* j : mOwned->mJointMotionArray)
            if (j->mJointName == name) return j;
        return nullptr;
    }
    JointMotion* ensureTrack(const std::string& name)
    {
        if (auto* j = track(name)) return j;
        auto* j = new JointMotion();
        j->mJointName = name;
        j->mJointKey = LLJoint::getAliasedJointKey(name);
        j->mPriority = mOwned->mBasePriority;
        j->mUsage = 0;
        mOwned->mJointMotionArray.push_back(j);
        return j;
    }
    bool canSerialize(LLCharacter* character) const
    {
        if (!character) return false;
        for (const auto* j : mOwned->mJointMotionArray)
            if (!j->mScaleCurve.mKeys.empty()) return false;
        for (const auto* c : mOwned->mConstraints)
        {
            if (!character->findCollisionVolume(c->mSourceConstraintVolId)) return false;
            if (c->mConstraintTargetType != CONSTRAINT_TARGET_TYPE_GROUND &&
                !character->findCollisionVolume(c->mTargetConstraintVolId)) return false;
        }
        return true;
    }
private:
    std::unique_ptr<JointMotionList> mOwned;
};

FSFloaterPoser::~FSFloaterPoser() = default;
FSFloaterPoser::FSFloaterPoser(const LLSD& key) : LLFloater("Firestorm Poser")
{
    // Bind requests, other controls are find-and-binds, see postBuild()
    mCommitCallbackRegistrar.add("Poser.RefreshAvatars", [this](LLUICtrl*, const LLSD&) { onAvatarsRefresh(); });
    mCommitCallbackRegistrar.add("Poser.StartStopAnimating", [this](LLUICtrl*, const LLSD&) { onPoseStartStop(); });
    mCommitCallbackRegistrar.add("Poser.ToggleLoadSavePanel", [this](LLUICtrl*, const LLSD&) { onToggleLoadSavePanel(); });
    mCommitCallbackRegistrar.add("Poser.ToggleVisualManipulators", [this](LLUICtrl*, const LLSD&) { onToggleVisualManipulators(); });
    mCommitCallbackRegistrar.add("Poser.ToggleRotationFrame", [this](LLUICtrl* button, const LLSD&) { onToggleRotationFrameButton(button); });

    mCommitCallbackRegistrar.add("Poser.UndoLastRotation", [this](LLUICtrl*, const LLSD&) { onUndoLastChange(); });
    mCommitCallbackRegistrar.add("Poser.RedoLastRotation", [this](LLUICtrl*, const LLSD&) { onRedoLastChange(); });
    mCommitCallbackRegistrar.add("Poser.ToggleMirrorChanges", [this](LLUICtrl*, const LLSD&) { onToggleMirrorChange(); });
    mCommitCallbackRegistrar.add("Poser.ToggleSympatheticChanges", [this](LLUICtrl*, const LLSD&) { onToggleSympatheticChange(); });
    mCommitCallbackRegistrar.add("Poser.AdjustTrackPadSensitivity", [this](LLUICtrl*, const LLSD&) { onAdjustTrackpadSensitivity(); });

    mCommitCallbackRegistrar.add("Poser.PositionSet", [this](LLUICtrl*, const LLSD&) { onPositionSet(); });
    mCommitCallbackRegistrar.add("Poser.SetToTPose", [this](LLUICtrl*, const LLSD&) { onSetAvatarToTpose(); });

    mCommitCallbackRegistrar.add("Poser.Advanced.ScaleSet", [this](LLUICtrl*, const LLSD&) { onScaleSet(); });
    mCommitCallbackRegistrar.add("Poser.UndoLastPosition", [this](LLUICtrl*, const LLSD&) { onUndoLastChange(); });
    mCommitCallbackRegistrar.add("Poser.RedoLastPosition", [this](LLUICtrl*, const LLSD&) { onRedoLastChange(); });
    mCommitCallbackRegistrar.add("Poser.ResetJoint", [this](LLUICtrl*, const LLSD& data) { onResetJoint(data); });

    mCommitCallbackRegistrar.add("Poser.Save", [this](LLUICtrl*, const LLSD&) { onClickPoseSave(); });
    mCommitCallbackRegistrar.add("Pose.Menu", [this](LLUICtrl*, const LLSD& data) { onPoseMenuAction(data); });
    mCommitCallbackRegistrar.add("Poser.BrowseCache", [this](LLUICtrl*, const LLSD&) { onClickBrowsePoseCache(); });
    mCommitCallbackRegistrar.add("Poser.LoadLeftHand", [this](LLUICtrl*, const LLSD&) { onClickLoadLeftHandPose(); });
    mCommitCallbackRegistrar.add("Poser.LoadRightHand", [this](LLUICtrl*, const LLSD&) { onClickLoadRightHandPose(); });

    mCommitCallbackRegistrar.add("Poser.FlipPose", [this](LLUICtrl*, const LLSD&) { onClickFlipPose(); });
    mCommitCallbackRegistrar.add("Poser.FlipJoint", [this](LLUICtrl*, const LLSD&) { onClickFlipSelectedJoints(); });
    mCommitCallbackRegistrar.add("Poser.RecaptureSelectedBones", [this](LLUICtrl*, const LLSD&) { onClickRecaptureSelectedBones(); });
    mCommitCallbackRegistrar.add("Poser.TogglePosingSelectedBones", [this](LLUICtrl*, const LLSD&) { onClickToggleSelectedBoneEnabled(); });

    mCommitCallbackRegistrar.add("Poser.CommitSpinner", [this](LLUICtrl* spinner, const LLSD& data) { onCommitSpinner(spinner, data); });
    mCommitCallbackRegistrar.add("Poser.CommitSlider", [this](LLUICtrl* slider, const LLSD& data) { onCommitSlider(slider, data); });
    mCommitCallbackRegistrar.add("Poser.Symmetrize", [this](LLUICtrl*, const LLSD& data) { onClickSymmetrize(data); });

    mLoadPoseTimer = new FSLoadPoseTimer(boost::bind(&FSFloaterPoser::timedReload, this));
    // Reuse Firestorm's original layout; translated XML only adapts the
    // legacy tag/callback grammar, not the Poser controls or algorithms.
    LLUICtrlFactory::getInstance()->buildFloater(this, "floater_fs_poser_chaos.xml", NULL, false);
}

bool FSFloaterPoser::postBuild()
{
    mAvatarTrackball = getChild<FSVirtualTrackpad>("limb_rotation");
    mAvatarTrackball->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mAvatarTrackball->setCallbackUserData(this);

    mJointsTabs = getChild<LLTabContainer>("joints_tabs");
    mJointsTabs->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mJointsTabs->setCallbackUserData(this);

    mAvatarSelectionScrollList = getChild<LLScrollListCtrl>("avatarSelection_scroll");
    mAvatarSelectionScrollList->setCommitOnSelectionChange(true);
    mAvatarSelectionScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mAvatarSelectionScrollList->setCallbackUserData(this);

    mBodyJointsScrollList = getChild<LLScrollListCtrl>("body_joints_scroll");
    mBodyJointsScrollList->setCommitOnSelectionChange(true);
    mBodyJointsScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mBodyJointsScrollList->setCallbackUserData(this);

    mFaceJointsScrollList = getChild<LLScrollListCtrl>("face_joints_scroll");
    mFaceJointsScrollList->setCommitOnSelectionChange(true);
    mFaceJointsScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mFaceJointsScrollList->setCallbackUserData(this);

    mHandJointsScrollList = getChild<LLScrollListCtrl>("hand_joints_scroll");
    mHandJointsScrollList->setCommitOnSelectionChange(true);
    mHandJointsScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mHandJointsScrollList->setCallbackUserData(this);

    mMiscJointsScrollList = getChild<LLScrollListCtrl>("misc_joints_scroll");
    mMiscJointsScrollList->setCommitOnSelectionChange(true);
    mMiscJointsScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mMiscJointsScrollList->setCallbackUserData(this);

    mCollisionVolumesScrollList = getChild<LLScrollListCtrl>("collision_volumes_scroll");
    mCollisionVolumesScrollList->setCommitOnSelectionChange(true);
    mCollisionVolumesScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mCollisionVolumesScrollList->setCallbackUserData(this);

    mEntireAvJointScroll = getChild<LLScrollListCtrl>("entireAv_joint_scroll");

    mPosesScrollList = getChild<LLScrollListCtrl>("poses_scroll");
    mPosesScrollList->setCommitOnSelectionChange(true);
    mPosesScrollList->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mPosesScrollList->setCallbackUserData(this);

    mToggleVisualManipulators = getChild<LLButton>("toggleVisualManipulators");
    mToggleVisualManipulators->setToggleState(true);

    mTrackpadSensitivitySlider = getChild<LLSliderCtrl>("trackpad_sensitivity_slider");

    mPoseSaveNameEditor = getChild<LLLineEditor>("pose_save_name");
    mPoseSaveNameEditor->setPrevalidate(&validatePoserFilename);

    mHandPresetsScrollList = getChild<LLScrollListCtrl>("hand_presets_scroll");

    mPosXSlider = getChild<LLSliderCtrl>("av_position_inout");
    mPosYSlider = getChild<LLSliderCtrl>("av_position_leftright");
    mPosZSlider = getChild<LLSliderCtrl>("av_position_updown");

    mAdvRotXSlider = getChild<LLSliderCtrl>("limb_pitch_slider");
    mAdvRotYSlider = getChild<LLSliderCtrl>("limb_yaw_slider");
    mAdvRotZSlider = getChild<LLSliderCtrl>("limb_roll_slider");

    mAdvPosXSlider = getChild<LLSliderCtrl>("Advanced_Position_X");
    mAdvPosYSlider = getChild<LLSliderCtrl>("Advanced_Position_Y");
    mAdvPosZSlider = getChild<LLSliderCtrl>("Advanced_Position_Z");

    mAdvScaleXSlider = getChild<LLSliderCtrl>("Advanced_Scale_X");
    mAdvScaleYSlider = getChild<LLSliderCtrl>("Advanced_Scale_Y");
    mAdvScaleZSlider = getChild<LLSliderCtrl>("Advanced_Scale_Z");

    mPosesLoadSavePnl = getChild<LLPanel>("poses_loadSave");
    mStartStopPosingBtn = getChild<LLButton>("start_stop_posing_button");
    mToggleLoadSavePanelBtn = getChild<LLButton>("toggleLoadSavePanel");
    mBrowserFolderBtn = getChild<LLButton>("open_poseDir_button");
    mLoadPosesBtn = getChild<LLButton>("load_poses_button");
    mSavePosesBtn = getChild<LLButton>("save_poses_button");
    // Cool VL's LLButton lacks the modern mouse-leave callback. The
    // original save action and state indicator remain intact.

    mFlipPoseBtn = getChild<LLButton>("FlipPose_avatar");
    mFlipJointBtn = getChild<LLButton>("FlipJoint_avatar");
    mRecaptureBtn = getChild<LLButton>("button_RecaptureParts");
    mTogglePosingBonesBtn = getChild<LLButton>("toggle_PosingSelectedBones");
    mToggleLockWorldRotBtn = getChild<LLButton>("toggle_LockWorldRotation");
    mToggleLockWorldRotBtn->setClickedCallback(&FSFloaterPoser::onLegacyLockWorldRotation, this);

    mToggleMirrorRotationBtn = getChild<LLButton>("button_toggleMirrorRotation");
    mToggleSympatheticRotationBtn = getChild<LLButton>("button_toggleSympatheticRotation");
    mToggleDeltaModeBtn = getChild<LLButton>("delta_mode_toggle");
    mRedoChangeBtn = getChild<LLButton>("button_redo_change");
    mUndoChangeBtn = getChild<LLButton>("undo_change");
    mSetToTposeButton = getChild<LLButton>("set_t_pose_button");
    mBtnJointReset = getChild<LLButton>("poser_joint_reset");

    mBtnWorldFrame = getChild<LLButton>("poser_world_frame_toggle");
    mBtnAvatarFrame = getChild<LLButton>("poser_avatar_frame_toggle");
    mBtnScreenFrame = getChild<LLButton>("poser_screen_frame_toggle");

    mJointsParentPnl = getChild<LLPanel>("joints_parent_panel");
    mTrackballPnl = getChild<LLPanel>("trackball_panel");
    mPositionPnl = getChild<LLPanel>("position_panel");
    mMoveTabPnl = getChild<LLPanel>("move_tab_panel");
    mTrackballButtonPnl = getChild<LLPanel>("trackball_button_panel");
    mPositionRotationPnl = getChild<LLPanel>("positionRotation_panel");
    mBodyJointsPnl = getChild<LLPanel>("body_joints_panel");
    mFaceJointsPnl = getChild<LLPanel>("face_joints_panel");
    mHandsTabs = getChild<LLTabContainer>("hands_tabs");
    mHandsJointsPnl = mHandsTabs->getChild<LLPanel>("hands_joints_panel");
    mMiscJointsPnl = getChild<LLPanel>("misc_joints_panel");
    mCollisionVolumesPnl = getChild<LLPanel>("collision_volumes_panel");

    mUnlockPelvisInBvhSaveCbx = getChild<LLCheckBoxCtrl>("unlock_pelvis_for_bvh_save_checkbox");
    mUnlockPelvisInBvhSaveCbx->setVisible(getSavingToBvh());
    mAlsoSaveBvhCbx = getChild<LLCheckBoxCtrl>("also_save_bvh_checkbox");
    mAlsoSaveBvhCbx->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mAlsoSaveBvhCbx->setCallbackUserData(this);

    mTrackpadSensitivitySpnr = getChild<LLUICtrl>("trackpad_sensitivity_spinner");
    mYawSpnr                 = getChild<LLUICtrl>("limb_yaw_spinner");
    mYawSpnr->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mYawSpnr->setCallbackUserData(this);
    mPitchSpnr               = getChild<LLUICtrl>("limb_pitch_spinner");
    mPitchSpnr->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mPitchSpnr->setCallbackUserData(this);
    mRollSpnr = getChild<LLUICtrl>("limb_roll_spinner");
    mRollSpnr->setCommitCallback(&FSFloaterPoser::onLegacyDirectCommit);
    mRollSpnr->setCallbackUserData(this);
    mUpDownSpnr              = getChild<LLUICtrl>("av_position_updown_spinner");
    mLeftRightSpnr           = getChild<LLUICtrl>("av_position_leftright_spinner");
    mInOutSpnr               = getChild<LLUICtrl>("av_position_inout_spinner");
    mAdvPosXSpnr             = getChild<LLUICtrl>("adv_posx_spinner");
    mAdvPosYSpnr             = getChild<LLUICtrl>("adv_posy_spinner");
    mAdvPosZSpnr             = getChild<LLUICtrl>("adv_posz_spinner");
    mScaleXSpnr              = getChild<LLUICtrl>("adv_scalex_spinner");
    mScaleYSpnr              = getChild<LLUICtrl>("adv_scaley_spinner");
    mScaleZSpnr              = getChild<LLUICtrl>("adv_scalez_spinner");

    // Firestorm 7.2.5 references a removed XUI button here.
    // mBtnJointRotate is unused; do not dereference a missing control.
    bindLegacyActions();
    mLoadPosesBtn->setClickedCallback(&FSFloaterPoser::onShowPoseMenu, this);
    mRecordStartButton = getChild<LLButton>("poser_record_start");
    mRecordStopButton = getChild<LLButton>("poser_record_stop");
    mRecordExportButton = getChild<LLButton>("poser_record_export");
    mStaticExportButton = getChild<LLButton>("poser_static_export");
    mStaticExportButton->setClickedCallback(&FSFloaterPoser::onStaticExportClicked, this);
    mRecordStatusText = getChild<LLTextBox>("poser_record_status");
    mRecordStartButton->setClickedCallback(&FSFloaterPoser::onRecordStartClicked, this);
    mRecordStopButton->setClickedCallback(&FSFloaterPoser::onRecordStopClicked, this);
    mRecordExportButton->setClickedCallback(&FSFloaterPoser::onRecordExportClicked, this);
    mInventoryLoad = getChild<LLButton>("poser_inventory_load");
    mInventoryPlay = getChild<LLButton>("poser_inventory_play");
    mInventoryClear = getChild<LLButton>("poser_inventory_clear");
    mInventorySetKey = getChild<LLButton>("poser_inventory_setkey");
    mInventoryOffset = getChild<LLButton>("poser_inventory_offset");
    mInventoryExtend = getChild<LLButton>("poser_inventory_extend");
    mInventoryPrev = getChild<LLButton>("poser_inventory_prev");
    mInventoryNext = getChild<LLButton>("poser_inventory_next");
    mInventoryExport = getChild<LLButton>("poser_inventory_export");
    mInventoryTimeline = getChild<LLSliderCtrl>("poser_inventory_timeline");
    mInventoryStatus = getChild<LLTextBox>("poser_inventory_status");
    mInventoryTimeText = getChild<LLTextBox>("poser_inventory_time");
    mInventoryLoad->setClickedCallback(&FSFloaterPoser::onInventoryLoad, this);
    mInventoryPlay->setClickedCallback(&FSFloaterPoser::onInventoryPlay, this);
    mInventoryClear->setClickedCallback(&FSFloaterPoser::onInventoryClear, this);
    mInventorySetKey->setClickedCallback(&FSFloaterPoser::onInventorySetKey, this);
    mInventoryOffset->setClickedCallback(&FSFloaterPoser::onInventoryOffset, this);
    mInventoryExtend->setClickedCallback(&FSFloaterPoser::onInventoryExtend, this);
    mInventoryPrev->setClickedCallback(&FSFloaterPoser::onInventoryPrev, this);
    mInventoryNext->setClickedCallback(&FSFloaterPoser::onInventoryNext, this);
    mInventoryExport->setClickedCallback(&FSFloaterPoser::onInventoryExport, this);
    mInventoryTimeline->setCommitCallback(&FSFloaterPoser::onInventorySeek);
    mInventoryTimeline->setCallbackUserData(this);
    updateRecorderButtons();
    return true;
}

void FSFloaterPoser::onOpen()
{
    // Cool VL's singleton window is hidden and reopened; the timer was
    // deleted on close and must be recreated before any callbacks use it.
    if (!mLoadPoseTimer)
        mLoadPoseTimer = new FSLoadPoseTimer(boost::bind(&FSFloaterPoser::timedReload, this));
    createUserPoseDirectoryIfNeeded();
    onAvatarsRefresh();
    refreshJointScrollListMembers();
    onJointTabSelect();
    refreshPoseScroll(mHandPresetsScrollList, POSE_PRESETS_HANDS_SUBDIRECTORY);
    startPosingSelf();

    enableVisualManipulators();
    LLFloater::onOpen();
}

void FSFloaterPoser::onFocusReceived()
{
    gEditMenuHandlerp = this;
}

void FSFloaterPoser::onFocusLost()
{
    if (gEditMenuHandlerp == this)
        gEditMenuHandlerp = nullptr;
}

void FSFloaterPoser::draw()
{
    tickInventoryEditor();
    updateRecorderButtons();
    if (mRecordingActive)
    {
        auto now = std::chrono::steady_clock::now();
        const auto elapsedMs = std::chrono::duration_cast<std::chrono::milliseconds>(now - mRecordingBegan).count();
        const size_t required = std::min<size_t>(3600, (size_t)std::max<long long>(0, elapsedMs) * 30 / 1000 + 1);
        if (required > mRecordedFrames.size())
        {
            // One actual joint sample per draw. Replicate it for skipped frames
            // to preserve real-time duration at the fixed 30 FPS export rate.
            // Hold the previous sample across missed draw ticks.
            while (!mRecordedFrames.empty() && mRecordedFrames.size() + 1 < required)
                mRecordedFrames.push_back(mRecordedFrames.back());
            recordCurrentFrame();
        }
        if (mRecordingActive && mRecordedFrames.size() >= 3600)
            stopRecording();
        else if (mRecordStatusText && mRecordingActive)
            mRecordStatusText->setText(llformat("REC %d:%02d", (int)(elapsedMs / 60000), (int)(elapsedMs / 1000 % 60)));
    }
    LLFloater::draw();
    drawOnHoverJointHint();
}

void FSFloaterPoser::markSelectedJointsToHighlight()
{
    bool toolsEnabled = mToggleVisualManipulators->getValue().asBoolean();
    if (toolsEnabled)
        return;

    bool showHighlights = gSavedSettings.getBool(std::string(POSER_SHOWBONEHIGHLIGHTS_SAVE_KEY).c_str());
    if (!showHighlights)
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    std::string jointName   = selectedJoints[0]->jointName();
    bool        isRightLimb = jointName.find("Right") != std::string::npos;
    bool        isLeftLimb  = jointName.find("Left") != std::string::npos;

    if (!(isRightLimb || isLeftLimb))
        return;

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    mLastSelectedJoint = selectedJoints[0];
    timeFadeStartedMicrosec = gFrameTime;
}

void FSFloaterPoser::drawOnHoverJointHint()
{
    if (!mLastSelectedJoint)
        return;

    constexpr U64 GLOW_TIME_US = 300000;
    U64           fadeTimeUs   = gFrameTime - timeFadeStartedMicrosec;
    if (fadeTimeUs > GLOW_TIME_US)
        return;

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    LLJoint* joint = avatar->getRootJoint()->findJoint(std::string(mLastSelectedJoint->jointName()));
    if (!joint)
        return;

    F32              alphaFade            = 1.f * (GLOW_TIME_US - fadeTimeUs) / GLOW_TIME_US;
    LLColor4 beaconColour = gColors.getColor4("AreaSearchBeaconColor");
    beaconColour.setAlpha(alphaFade);
    LLVector3        joint_world_position = joint->getWorldPosition();

    static LLCachedControl<S32> beacon_line_width(gSavedSettings, "DebugBeaconLineWidth");
    gObjectList.addDebugBeacon(joint_world_position, "", beaconColour, beaconColour, beacon_line_width);
}

void FSFloaterPoser::enableVisualManipulators()
{
    if (!gAgentAvatarp || gAgentAvatarp.isNull())
    {
        mToggleVisualManipulators->setToggleState(false);
        return;
    }

    if (gToolMgr.getCurrentToolset() != gCameraToolset)
        mLastToolset = gToolMgr.getCurrentToolset();

    gToolMgr.setCurrentToolset(gPoserToolset);
    gToolMgr.getCurrentToolset()->selectTool(FSToolCompPose::getInstance());

    LLVOAvatar* avatar = getUiSelectedAvatar();
    setVisualManipulators(avatar);
}

void FSFloaterPoser::disableVisualManipulators()
{
    if (mLastToolset)
        gToolMgr.setCurrentToolset(mLastToolset);

    setVisualManipulators(nullptr);
}

void FSFloaterPoser::setVisualManipulators(LLVOAvatar* avatar)
{
    FSToolCompPose::getInstance()->setAvatar(avatar);
    FSToolCompPoseTranslate::getInstance()->setAvatar(avatar);

    if (!avatar)
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    updateManipWithFirstSelectedJoint(selectedJoints);
}

void FSFloaterPoser::onClose(bool app_quitting)
{
    stopRecording();
    clearInventoryAnimation(false);
    // Self is a puppet only while the Poser window is open. Release the
    // animation override on close regardless of the legacy multi-avatar setting.
    if (gAgentAvatarp && mPoserAnimator.isPosingAvatar(gAgentAvatarp))
        mPoserAnimator.stopPosingAvatar(gAgentAvatarp);
    if (gSavedSettings.getBool(std::string(POSER_STOPPOSINGWHENCLOSED_SAVE_KEY).c_str()))
    {
        stopPosingAllAvatars();
    }

    disableVisualManipulators();
    delete mLoadPoseTimer;
    mLoadPoseTimer = nullptr;
    LLFloater::onClose(app_quitting);
}

void FSFloaterPoser::refreshPoseScroll(LLScrollListCtrl* posesScrollList, std::optional<std::string_view> subDirectory)
{
    if (!posesScrollList)
        return;

    posesScrollList->clearRows();

    std::string dir = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
    if (subDirectory.has_value())
        FSPoserFilePaths::append(dir, std::string(subDirectory.value()));

    // Firestorm's Poser XUI uses LLStringUtil's custom datetime token mapping.
    // Cool VL does not initialize that map on this path, so the token formatter
    // passes an empty strftime format into LLDate::toHTTPDateString and asserts.
    // Format *only the Poser file timestamps* with the system time API.
    // A 24-hour display is the default if Firestorm's clock setting is absent.
    const bool use_24_hour_clock =
        !gSavedSettings.controlExists("Use24HourClock") ||
        gSavedSettings.getBool("Use24HourClock");
    std::string   file;
    llstat        stat_data;
    time_t        last_modified = 0;
    LLDirIterator dir_iter(dir, POSE_INTERNAL_FORMAT_FILE_MASK);
    while (dir_iter.next(file))
    {
        std::string path = FSPoserFilePaths::add(dir, file);
        std::string name = FSPoserFilePaths::getBaseFileName(LLURI::unescape(path), true);

        if (LLFile::stat(path, &stat_data))
            last_modified = 0;
        else
            last_modified = stat_data.st_mtime;

        std::string date_str;
        if (last_modified > 0)
        {
            std::tm local_date{};
#if LL_WINDOWS
            const bool valid_local_date = ::localtime_s(&local_date, &last_modified) == 0;
#else
            const bool valid_local_date = ::localtime_r(&last_modified, &local_date) != nullptr;
#endif
            if (valid_local_date)
            {
                char date_buffer[64]{};
                const char* format = use_24_hour_clock
                    ? "%Y/%m/%d %H:%M:%S" : "%Y/%m/%d %I:%M:%S %p";
                if (std::strftime(date_buffer, sizeof(date_buffer), format, &local_date) != 0)
                    date_str.assign(date_buffer);
            }
        }
        if (date_str.empty())
            date_str = "-";

        LLSD row;
        row["columns"][0]["column"] = "load_file_name_column";
        row["columns"][0]["value"] = name;
        row["columns"][1]["column"] = "load_file_date_column";
        row["columns"][1]["value"] = date_str;
        row["columns"][2]["column"] = "load_file_date_sort_column";
        row["columns"][2]["value"] = llformat("%u", (U64)last_modified);

        llifstream infile(path.c_str());
        if (!infile.is_open())
        {
            llwarns << "Skipping: Cannot read file in: " << path << llendl;
            continue;
        }

        LLSD data;
        if (LLSDParser::PARSE_FAILURE == LLSDSerialize::fromXML(data, infile))
        {
            llwarns << "Skipping: Failed to parse pose file: " << path << llendl;
            continue;
        }

        posesScrollList->addElement(row);
    }
}

void FSFloaterPoser::onPoseFileSelect()
{
    LLScrollListItem* item = mPosesScrollList->getFirstSelected();
    if (!item)
        return;

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar))
        return;

    bool enableButtons = mPoserAnimator.isPosingAvatar(avatar);
    mLoadPosesBtn->setEnabled(enableButtons);
    mSavePosesBtn->setEnabled(enableButtons && inventoryExportAllowed());

    std::string poseName = item->getColumn(0)->getValue().asString();
    if (poseName.empty())
        return;

    std::string name = std::string(poseName);
    mPoseSaveNameEditor->setEnabled(enableButtons);
    mPoseSaveNameEditor->setText(name);

    bool isDeltaSave = !poseFileStartsFromTeePose(name);
    if (isDeltaSave)
        mLoadPosesBtn->setLabel(tryGetString("LoadDiffLabel"));
    else
        mLoadPosesBtn->setLabel(tryGetString("LoadPoseLabel"));
}

void FSFloaterPoser::doPoseSave(LLVOAvatar* avatar, const std::string& filename)
{
    if (!avatar)
        return;

    bool successfulSave = savePoseToXml(avatar, filename);
    if (successfulSave)
    {
        refreshPoseScroll(mPosesScrollList);
        setUiSelectedAvatarSaveFileName(filename);

        if (getSavingToBvh())
            savePoseToBvh(avatar, filename);

        mSavePosesBtn->setImageOverlay(tryGetString(ICON_SAVE_OK), LLFontGL::HCENTER);
        setSavePosesButtonText(!mPoserAnimator.allBaseRotationsAreZero(avatar));
    }
    else
    {
         mSavePosesBtn->setImageOverlay(tryGetString(ICON_SAVE_FAILED), LLFontGL::HCENTER);
    }
}

void FSFloaterPoser::onClickPoseSave()
{
    std::string filename = mPoseSaveNameEditor->getValue().asString();
    if (filename.empty())
    {
        mSavePosesBtn->setImageOverlay(tryGetString(ICON_SAVE_FAILED), LLFontGL::HCENTER);
        return;
    }

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!inventoryExportAllowed() || (avatar && mPosesWithUnknownSource.count(avatar->getID())))
    {
        gNotifications.add("ChaosPoserSaveDeniedCreator");
        return;
    }
    if (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar))
    {
        mSavePosesBtn->setImageOverlay(tryGetString(ICON_SAVE_FAILED), LLFontGL::HCENTER);
        return;
    }

    // if prompts are disabled or file doesn't exist, do the save immediately:
    const bool prompt = gSavedSettings.getBool(std::string(POSER_SAVECONFIRMREQUIRED_SAVE_KEY).c_str());

    std::string fullPath = FSPoserFilePaths::getExpandedFilename(
        LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, filename + POSE_INTERNAL_FORMAT_FILE_EXT);
    const bool exists = FSPoserFilePaths::fileExists(fullPath);

    if (!prompt || !exists)
    {
        // new file or no overwrite guard
        doPoseSave(avatar, filename);
    }
    else
    {
        // show a modal dialog, passing the pose name along
        LLSD args;
        args["POSE_NAME"] = filename;
       
        gNotifications.add("ConfirmPoserOverwrite", args, LLSD(), // no payload
            [this, avatar, filename](const LLSD& notification, const LLSD& response)
            {
                if (LLNotification::getSelectedOption(notification, response) == 0)
                {
                    // user clicked “Yes”
                    doPoseSave(avatar, filename);
                }
                // else do nothing (cancel)
            });
    }
}

void FSFloaterPoser::onMouseLeaveSavePoseBtn()
{
    mSavePosesBtn->setImageOverlay(tryGetString("icon_save_button"), LLFontGL::HCENTER);

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    setSavePosesButtonText(!mPoserAnimator.allBaseRotationsAreZero(avatar));
}

void FSFloaterPoser::createUserPoseDirectoryIfNeeded()
{
    std::string userPath =
        FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);

    std::string userHandPresetsPath = userPath + FSPoserFilePaths::getDirDelimiter() + std::string(POSE_PRESETS_HANDS_SUBDIRECTORY);

    try
    {
        if (!FSPoserFilePaths::fileExists(userHandPresetsPath))
        {
            if (!FSPoserFilePaths::fileExists(userPath))
            {
                llwarns << "Couldn't find folder: " << userPath << " - creating one." << llendl;
                LLFile::mkdir(userPath);
            }

            if (!FSPoserFilePaths::fileExists(userHandPresetsPath))
            {
                llwarns << "Couldn't find folder: " << userHandPresetsPath << " - creating one." << llendl;
                LLFile::mkdir(userHandPresetsPath);
            }
        }

        std::string sourcePresetPath =
            FSPoserFilePaths::getExpandedFilename(LL_PATH_APP_SETTINGS, POSE_SAVE_SUBDIRECTORY, std::string(POSE_PRESETS_HANDS_SUBDIRECTORY));

        if (!FSPoserFilePaths::fileExists(sourcePresetPath))
        {
            llwarns << "Can not copy poser presets because failed to find path: " << sourcePresetPath << llendl;
            return;
        }

        auto posesToCopy = FSPoserFilePaths::getFilesInDir(sourcePresetPath);
        for (const auto& pose : posesToCopy)
        {
            std::string source      = sourcePresetPath + FSPoserFilePaths::getDirDelimiter() + pose;
            std::string destination = userHandPresetsPath + FSPoserFilePaths::getDirDelimiter() + pose;

            S32 sourceVersion = tryGetPoseVersion(source);
            S32 destinationVersion = tryGetPoseVersion(destination);
            if (destinationVersion >= sourceVersion)
                continue;

            if (FSPoserFilePaths::fileExists(destination))
            {
                llwarns << "Removing pose file " << destination << " to replace with updated version " << llendl;
                LLFile::remove(destination);
            }

            if (!LLFile::copy(source, destination))
                llwarns << "Failed to copy " << source << " to " << destination << llendl;
        }
    }
    catch (const std::exception& e)
    {
        llwarns << "Exception caught trying to create/update poses: " << e.what() << llendl;
    }
}

bool FSFloaterPoser::savePoseToXml(LLVOAvatar* avatar, const std::string& poseFileName)
{
    if (poseFileName.empty())
        return false;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return false;

    if (!inventoryExportAllowed() || mPosesWithUnknownSource.count(avatar->getID()))
        return false;  // Unknown external pose creator: fail closed.

    createUserPoseDirectoryIfNeeded();

    try
    {
        bool savingDiff = !mPoserAnimator.allBaseRotationsAreZero(avatar);
        LLSD record;
        record["version"]["value"] = (S32)8;
        record["startFromTeePose"]["value"] = !savingDiff;

        if (savingDiff)
            mPoserAnimator.savePosingState(avatar, false, &record);

        LLVector3 rotation, position, scale, zeroVector;
        bool      baseRotationIsZero, userSetBaseRotationToZero;

        for (const FSPoserAnimator::FSPoserJoint& pj : mPoserAnimator.PoserJoints)
        {
            std::string bone_name = pj.jointName();
            bool posingThisJoint  = mPoserAnimator.isPosingAvatarJoint(avatar, pj);
            bool jointRotLocked   = mPoserAnimator.getRotationIsWorldLocked(avatar, pj);
            bool jointRotMirrored = mPoserAnimator.getRotationIsMirrored(avatar, pj);

            record[bone_name]             = bone_name;
            record[bone_name]["enabled"]  = posingThisJoint;
            if (!posingThisJoint)
                continue;

            record[bone_name]["mirrored"] = jointRotMirrored;

            if (!mPoserAnimator.tryGetJointSaveVectors(avatar, pj, &rotation, &position, &scale, &baseRotationIsZero,
                                                       &userSetBaseRotationToZero))
                continue;

            bool jointRotPosScaleAllZero = rotation == zeroVector && position == zeroVector && scale == zeroVector;

            if (savingDiff && jointRotPosScaleAllZero)
                continue;

            record[bone_name]["jointBaseRotationIsZero"]   = baseRotationIsZero;
            record[bone_name]["userSetBaseRotationToZero"] = userSetBaseRotationToZero;
            record[bone_name]["rotation"]                  = rotation.getValue();
            record[bone_name]["position"]                  = position.getValue();
            record[bone_name]["scale"]                     = scale.getValue();
            record[bone_name]["worldLocked"]               = jointRotLocked;
        }

        std::string fullSavePath =
            FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, poseFileName + POSE_INTERNAL_FORMAT_FILE_EXT);

        llofstream file;
        file.open(fullSavePath.c_str());
        if (!file.is_open())
        {
            llwarns << "Unable to save pose!" << llendl;
            return false;
        }
        LLSDSerialize::toPrettyXML(record, file);
        file.close();
    }
    catch (const std::exception& e)
    {
        llwarns << "Exception caught in saveToXml: " << e.what() << llendl;
        return false;
    }

    return true;
}

void FSFloaterPoser::onClickToggleSelectedBoneEnabled()
{
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        mPoserAnimator.setPosingAvatarJoint(avatar, *item, !currentlyPosing);
    }

    refreshRotationSlidersAndSpinners();
    refreshTrackpadCursor();
    refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::onClickFlipSelectedJoints()
{
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    for (auto item : selectedJoints)
    {
        // need to be posing the joint to flip
        bool currentlyPosingJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosingJoint)
            continue;

        auto oppositeJoint = mPoserAnimator.getPoserJointByName(item->mirrorJointName());
        if (oppositeJoint)
        {
            bool currentlyPosingOppositeJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *oppositeJoint);
            if (!currentlyPosingOppositeJoint)
                continue;
        }

        // If one selects a joint and its opposite, this would otherwise flip each of them sequentially to yeild no net result.
        // This allows the user to select a joint and its opposite, or only select one joint, and get the same result.
        if (std::find(selectedJoints.begin(), selectedJoints.end(), oppositeJoint) != selectedJoints.end())
        {
            if (!item->dontFlipOnMirror())
                continue;
        }

        mPoserAnimator.reflectJoint(avatar, item);
    }

    refreshRotationSlidersAndSpinners();
    enableOrDisableRedoAndUndoButton();
    refreshTrackpadCursor();
}

void FSFloaterPoser::onClickFlipPose()
{
    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    mPoserAnimator.flipEntirePose(avatar);

    refreshRotationSlidersAndSpinners();
    refreshTrackpadCursor();
}

void FSFloaterPoser::onClickRecaptureSelectedBones()
{
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    mPoserAnimator.updatePosingState(avatar, selectedJoints);

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (currentlyPosing)
            continue;

        mPoserAnimator.recaptureJoint(avatar, *item);
    }

    setSavePosesButtonText(true);
    refreshRotationSlidersAndSpinners();
    refreshPositionSlidersAndSpinners();
    refreshScaleSlidersAndSpinners();
    refreshTrackpadCursor();
    refreshTextHighlightingOnJointScrollLists();
    enableOrDisableRedoAndUndoButton();
}

void FSFloaterPoser::updatePosedBones(const std::string& jointName, const LLQuaternion& rotation, const LLVector3& position, const LLVector3& scale)
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    bool haveImplicitPermission = havePermissionToAnimateAvatar(avatar); // self & control avatars you own
    bool iCanPoseThem           = havePermissionToAnimateOtherAvatar(avatar);
    if (!haveImplicitPermission && !iCanPoseThem)
        return;

    const FSPoserAnimator::FSPoserJoint* poserJoint = mPoserAnimator.getPoserJointByName(jointName);
    if (!poserJoint)
        return;

    bool                   savingToExternal = getSavingToBvh();
    E_PoserReferenceFrame  frame            = getReferenceFrame();
    E_BoneDeflectionStyles defl             = getUiSelectedBoneDeflectionStyle();
    mPoserAnimator.updateJointFromManip(avatar, poserJoint, savingToExternal, defl, frame, rotation, position, scale); 

    refreshRotationSlidersAndSpinners();
    refreshPositionSlidersAndSpinners();
    refreshScaleSlidersAndSpinners();
    refreshTrackpadCursor();
    enableOrDisableRedoAndUndoButton();
    refreshTextHighlightingOnJointScrollLists();
}

LLQuaternion FSFloaterPoser::getManipGimbalRotation(const std::string& jointName)
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return LLQuaternion();

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return LLQuaternion();

    const FSPoserAnimator::FSPoserJoint* poserJoint = mPoserAnimator.getPoserJointByName(jointName);
    if (!poserJoint)
        return LLQuaternion();

    E_PoserReferenceFrame frame = getReferenceFrame();

    return mPoserAnimator.getManipGimbalRotation(avatar, poserJoint, frame);
}

void FSFloaterPoser::onClickBrowsePoseCache()
{
    createUserPoseDirectoryIfNeeded();

    std::string pathname = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
    // Windows desktop: open the actual pose folder via the system shell.
    const int utf16_chars = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
            pathname.c_str(), -1, nullptr, 0);
    if (utf16_chars > 0)
    {
        std::wstring utf16_path(static_cast<size_t>(utf16_chars), L'\0');
        if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS,
                pathname.c_str(), -1, utf16_path.data(), utf16_chars) > 0)
        {
            ShellExecuteW(nullptr, L"open", utf16_path.c_str(),
                          nullptr, nullptr, SW_SHOWNORMAL);
        }
    }
}

void FSFloaterPoser::onClickSymmetrize(const S32 ID)
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    mPoserAnimator.symmetrizeLeftToRightOrRightToLeft(avatar, ID == 2);

    refreshRotationSlidersAndSpinners();
    enableOrDisableRedoAndUndoButton();
    refreshTrackpadCursor();

    if (getSavingToBvh())
        refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::onCommitSpinner(const LLUICtrl* spinner, const S32 id)
{
    if (!spinner)
        return;

    F32 value = (F32)spinner->getValue().asReal();

    switch (id)
    {
        case 0: // av_position_updown_spinner
        {
            mAdvPosZSpnr->setValue(value);
            onPositionSet();
            break;
        }
        case 1: // av_position_leftright
        {
            mAdvPosYSpnr->setValue(value);
            onPositionSet();
            break;
        }
        case 2: // av_position_inout_spinner
        {
            mAdvPosXSpnr->setValue(value);
            onPositionSet();
            break;
        }
        case 3: // trackpad_sensitivity_spinner
        {
            onAdjustTrackpadSensitivity();
            break;
        }
        case 7: // adv_posx_spinner
        case 8: // adv_posy_spinner
        case 9: // adv_posz_spinner
        {
            onPositionSet();
            break;
        }
        case 10: // adv_scalex_spinner
        case 11: // adv_scaley_spinner
        case 12: // adv_scalez_spinner
        {
            onScaleSet();
            break;
        }

        default:
            llwarns << "onCommitSpinner passed invalid parameter: " << id << llendl;
            break;
    }
}

void FSFloaterPoser::onCommitSlider(const LLUICtrl* slider, const S32 id)
{
    if (!slider)
        return;

    F32 value = (F32)slider->getValue().asReal();

    switch (id)
    {
        case 0: // av_position_updown
        case 9: // Advanced_Position_Z
        {
            mAdvPosZSpnr->setValue(value);
            onPositionSet();
            break;
        }

        case 1: // av_position_leftright
        case 8: // Advanced_Position_Y
        {
            mAdvPosYSpnr->setValue(value);
            onPositionSet();
            break;
        }

        case 2: // av_position_inout
        case 7: // Advanced_Position_X
        {
            mAdvPosXSpnr->setValue(value);
            onPositionSet();
            break;
        }

        case 4: // limb_pitch_slider
        {
            mPitchSpnr->setValue(value);
            onYawPitchRollChanged();
            break;
        }
        case 5: // limb_yaw_slider
        {
            mYawSpnr->setValue(value);
            onYawPitchRollChanged();
            break;
        }
        case 6: // limb_roll_slider
        {
            mRollSpnr->setValue(value);
            onYawPitchRollChanged();
            break;
        }

        case 10: // Advanced_Scale_X
        {
            mScaleXSpnr->setValue(value);
            onScaleSet();
            break;
        }
        case 11: // Advanced_Scale_Y
        {
            mScaleYSpnr->setValue(value);
            onScaleSet();
            break;
        }
        case 12: // Advanced_Scale_Z
        {
            mScaleZSpnr->setValue(value);
            onScaleSet();
            break;
        }

        default:
            llwarns << "onCommitSlider passed invalid parameter: " << id << llendl;
            break;
    }
}

void FSFloaterPoser::onPoseMenuAction(const LLSD& param)
{
    std::string loadStyle = param.asString();
    if (loadStyle.empty())
        return;

    LLScrollListItem* item = mPosesScrollList->getFirstSelected();
    if (!item)
        return;

    std::string poseName = item->getColumn(0)->getValue().asString();

    E_LoadPoseMethods loadType = ROT_POS_AND_SCALES; // the default is to load everything
    if (loadStyle == "rotation")
        loadType = ROTATIONS;
    else if (loadStyle == "position")
        loadType = POSITIONS;
    else if (loadStyle == "scale")
        loadType = SCALES;
    else if (loadStyle == "rot_pos")
        loadType = ROTATIONS_AND_POSITIONS;
    else if (loadStyle == "rot_scale")
        loadType = ROTATIONS_AND_SCALES;
    else if (loadStyle == "pos_scale")
        loadType = POSITIONS_AND_SCALES;
    else if (loadStyle == "all")
        loadType = ROT_POS_AND_SCALES;
    else if (loadStyle == "selective")
        loadType = SELECTIVE;
    else if (loadStyle == "selective_rot")
        loadType = SELECTIVE_ROT;

    mLoadPoseTimer->tryLoading(poseName, loadType);
    setLoadingProgress(true);
}

void FSFloaterPoser::timedReload()
{
    if (!mLoadPoseTimer)
        return;

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar))
        return;

    if (loadPoseFromXml(avatar, mLoadPoseTimer->getPosePath(), mLoadPoseTimer->getLoadMethod()))
    {
        mLoadPoseTimer->completeLoading();
        onJointTabSelect();
        refreshJointScrollListMembers();
        setSavePosesButtonText(!mPoserAnimator.allBaseRotationsAreZero(avatar));
    }

    if (mLoadPoseTimer->loadCompleteOrFailed())
        setLoadingProgress(false);
}

void FSFloaterPoser::setLoadingProgress(bool started)
{
    LLLoadingIndicator* mLoadingIndicator = getChild<LLLoadingIndicator>("progress_indicator");
    if (!mLoadingIndicator)
        return;

    mLoadingIndicator->setVisible(started);
    started ? mLoadingIndicator->start() : mLoadingIndicator->stop();
}

void FSFloaterPoser::onClickLoadLeftHandPose()
{
    onClickLoadHandPose(false);
}

void FSFloaterPoser::onClickLoadRightHandPose()
{
    onClickLoadHandPose(true);
}

S32 FSFloaterPoser::tryGetPoseVersion(std::string pathToPoseFile)
{
    S32 version = -1;
    if (pathToPoseFile.empty())
        return version;

    if (!FSPoserFilePaths::fileExists(pathToPoseFile))
        return version;

    try
    {
        LLSD       pose;
        llifstream infile;

        infile.open(pathToPoseFile);
        if (!infile.is_open())
            return version;

        while (!infile.eof())
        {
            S32 lineCount = LLSDSerialize::fromXML(pose, infile);
            if (lineCount == LLSDParser::PARSE_FAILURE)
            {
                llwarns << "Failed to parse pose file: " << pathToPoseFile << llendl;
                return version;
            }

            for (LLSD::map_const_iterator itr = pose.beginMap(); itr != pose.endMap(); ++itr)
            {
                std::string const& name        = itr->first;
                LLSD const&        control_map = itr->second;

                if (name == "version")
                    return (S32)control_map["value"].asInteger();
            }
        }
    }
    catch (const std::exception& e)
    {
        llwarns << "Threw an exception trying read the pose file: " << pathToPoseFile << " exception: " << e.what() << llendl;
    }

    return version;
}

void FSFloaterPoser::onClickLoadHandPose(bool isRightHand)
{
    LLScrollListItem* item = mHandPresetsScrollList->getFirstSelected();
    if (!item)
        return;

    std::string poseName = item->getColumn(0)->getValue().asString();
    std::string pathname = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, std::string(POSE_PRESETS_HANDS_SUBDIRECTORY));
    if (!FSPoserFilePaths::fileExists(pathname))
        return;

    std::string fullPath =
        FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, std::string(POSE_PRESETS_HANDS_SUBDIRECTORY), poseName + POSE_INTERNAL_FORMAT_FILE_EXT);

    LLVOAvatar* avatar   = getUiSelectedAvatar();
    if (!avatar)
        return;
    if (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar))
        return;
    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    try
    {
        LLSD       pose;
        llifstream infile;
        LLVector3  vec3;

        infile.open(fullPath);
        if (!infile.is_open())
            return;

        while (!infile.eof())
        {
            S32 lineCount = LLSDSerialize::fromXML(pose, infile);
            if (lineCount == LLSDParser::PARSE_FAILURE)
            {
                llwarns << "Failed to parse loading a file for a hand: " << poseName << llendl;
                return;
            }

            for (LLSD::map_const_iterator itr = pose.beginMap(); itr != pose.endMap(); ++itr)
            {
                std::string const& name        = itr->first;
                LLSD const&        control_map = itr->second;

                if (name.find("Hand") == std::string::npos)
                    continue;
                if (isRightHand != (name.find("Left") == std::string::npos))
                    continue;

                const FSPoserAnimator::FSPoserJoint* poserJoint = mPoserAnimator.getPoserJointByName(name);
                if (!poserJoint)
                    continue;

                vec3.setValue(control_map["rotation"]);
                mPoserAnimator.loadJointRotation(avatar, poserJoint, true, true, vec3);
            }
        }

        addBoldToScrollList(mHandJointsScrollList, avatar);
    }
    catch ( const std::exception& e )
    {
        llwarns << "Threw an exception trying to load a hand pose: " << poseName << " exception: " << e.what() << llendl;
    }
}

bool FSFloaterPoser::poseFileStartsFromTeePose(const std::string& poseFileName)
{
    std::string pathname = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
    if (!FSPoserFilePaths::fileExists(pathname))
        return false;

    std::string fullPath =
        FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, poseFileName + POSE_INTERNAL_FORMAT_FILE_EXT);

    try
    {
        LLSD       pose;
        llifstream infile;
        bool       startFromZeroRot = false;

        infile.open(fullPath);
        if (!infile.is_open())
            return false;

        S32 lineCount = LLSDSerialize::fromXML(pose, infile);
        if (lineCount == LLSDParser::PARSE_FAILURE)
            return startFromZeroRot;

        for (LLSD::map_const_iterator itr = pose.beginMap(); itr != pose.endMap(); ++itr)
        {
            std::string const& name        = itr->first;
            LLSD const&        control_map = itr->second;

            if (name == "startFromTeePose")
                startFromZeroRot = control_map["value"].asBoolean();
        }

        return startFromZeroRot;
    }
    catch (const std::exception& e)
    {
        llwarns << "Unable to load or parse the pose: " << poseFileName << " exception: " << e.what() << llendl;
    }

    return false;
}

bool FSFloaterPoser::loadPoseFromXml(LLVOAvatar* avatar, const std::string& poseFileName, E_LoadPoseMethods loadMethod)
{
    bool        loadSuccess = false;
    std::string pathname = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
    if (!FSPoserFilePaths::fileExists(pathname))
        return loadSuccess;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return loadSuccess;

    std::string fullPath =
        FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, poseFileName + POSE_INTERNAL_FORMAT_FILE_EXT);

    bool loadRotations = loadMethod == ROTATIONS || loadMethod == ROTATIONS_AND_POSITIONS || loadMethod == ROTATIONS_AND_SCALES ||
                         loadMethod == ROT_POS_AND_SCALES || loadMethod == SELECTIVE || loadMethod == SELECTIVE_ROT;
    bool loadPositions = loadMethod == POSITIONS || loadMethod == ROTATIONS_AND_POSITIONS || loadMethod == POSITIONS_AND_SCALES ||
                         loadMethod == ROT_POS_AND_SCALES || loadMethod == SELECTIVE;
    bool loadScales    = loadMethod == SCALES || loadMethod == POSITIONS_AND_SCALES || loadMethod == ROTATIONS_AND_SCALES ||
                         loadMethod == ROT_POS_AND_SCALES || loadMethod == SELECTIVE;
    bool loadSelective = loadMethod == SELECTIVE || loadMethod == SELECTIVE_ROT;

    try
    {
        LLSD         pose;
        llifstream   infile;
        LLVector3    vec3;
        LLQuaternion quat;
        bool         enabled;
        bool         setJointBaseRotationToZero;
        bool         userSetBaseRotationToZero;
        bool         worldLocked;
        bool         mirroredJoint;
        S32          version = 0;
        bool startFromZeroRot = true;

        infile.open(fullPath);
        if (!infile.is_open())
            return loadSuccess;

        // Unknown provenance: an external XML's author is not verifiable.
        // Preserve loaded pose functionality, but deny subsequent export.
        mPosesWithUnknownSource.insert(avatar->getID());
        loadSuccess = true;
        while (!infile.eof())
        {
            S32 lineCount = LLSDSerialize::fromXML(pose, infile);
            if (lineCount == LLSDParser::PARSE_FAILURE)
            {
                llwarns << "Failed to parse file: " << poseFileName << llendl;
                return loadSuccess;
            }

            for (LLSD::map_const_iterator itr = pose.beginMap(); itr != pose.endMap(); ++itr)
            {
                std::string const& name        = itr->first;
                LLSD const&        control_map = itr->second;

                if (name == "startFromTeePose")
                    startFromZeroRot = control_map["value"].asBoolean();

                if (name == "version")
                    version = (S32)control_map["value"].asInteger();
            }

            if (version > 5 && startFromZeroRot)
                mPoserAnimator.setAllAvatarStartingRotationsToZero(avatar);

            bool loadPositionsAndScalesAsDeltas = false;
            if (version > 3)
                loadPositionsAndScalesAsDeltas = true;

            for (LLSD::map_const_iterator itr = pose.beginMap(); itr != pose.endMap(); ++itr)
            {
                std::string const& name        = itr->first;
                LLSD const&        control_map = itr->second;

                const FSPoserAnimator::FSPoserJoint *poserJoint = mPoserAnimator.getPoserJointByName(name);
                if (!poserJoint)
                    continue;

                if (loadSelective && mPoserAnimator.isPosingAvatarJoint(avatar, *poserJoint))
                    continue;

                if (control_map.has("enabled"))
                {
                    enabled = control_map["enabled"].asBoolean();
                    mPoserAnimator.setPosingAvatarJoint(avatar, *poserJoint, enabled || loadSelective);
                }

                if (control_map.has("jointBaseRotationIsZero"))
                    setJointBaseRotationToZero = control_map["jointBaseRotationIsZero"].asBoolean();
                else
                    setJointBaseRotationToZero = startFromZeroRot;

                if (control_map.has("userSetBaseRotationToZero"))
                    userSetBaseRotationToZero = control_map["userSetBaseRotationToZero"].asBoolean();
                else
                    userSetBaseRotationToZero = startFromZeroRot;

                if (loadPositions && control_map.has("position"))
                    vec3.setValue(control_map["position"]);
                else
                    vec3.clear();

                mPoserAnimator.loadJointPosition(avatar, poserJoint, loadPositionsAndScalesAsDeltas, vec3);

                if (loadRotations && control_map.has("rotation"))
                    vec3.setValue(control_map["rotation"]);
                else
                    vec3.clear();

                mPoserAnimator.loadJointRotation(avatar, poserJoint, setJointBaseRotationToZero, userSetBaseRotationToZero, vec3);

                if (loadScales && control_map.has("scale"))
                    vec3.setValue(control_map["scale"]);
                else
                    vec3.clear();

                mPoserAnimator.loadJointScale(avatar, poserJoint, loadPositionsAndScalesAsDeltas, vec3);

                worldLocked = control_map.has("worldLocked") ? control_map["worldLocked"].asBoolean() : false;
                mPoserAnimator.setRotationIsWorldLocked(avatar, *poserJoint, worldLocked);

                mirroredJoint = control_map.has("mirrored") ? control_map["mirrored"].asBoolean() : false;
                mPoserAnimator.setRotationIsMirrored(avatar, *poserJoint, mirroredJoint);
            }

            if (version > 6 && !startFromZeroRot && !loadSelective)
                loadSuccess = mPoserAnimator.loadPosingState(avatar, true, pose);
        }
    }
    catch ( const std::exception & e )
    {
        loadSuccess = false;
        llwarns << "Everything caught fire trying to load the pose: " << poseFileName << " exception: " << e.what() << llendl;
    }

    return loadSuccess;
}

void FSFloaterPoser::startPosingSelf()
{
    if (!gAgentAvatarp || gAgentAvatarp.isNull())
        return;

    setUiSelectedAvatar(gAgentAvatarp->getID());
    LLVOAvatar* avatar = getAvatarByUuid(gAgentAvatarp->getID());
    if (!avatar)
        return;

    bool arePosingSelected = mPoserAnimator.isPosingAvatar(avatar);
    if (!arePosingSelected && couldAnimateAvatar(avatar))
    {
        mPosesWithUnknownSource.erase(avatar->getID());
        mPoserAnimator.tryPosingAvatar(avatar);
    }

    onAvatarSelect();
}

void FSFloaterPoser::stopPosingAllAvatars()
{
    if (!gAgentAvatarp || gAgentAvatarp.isNull() || !mAvatarSelectionScrollList)
        return;

    for (auto listItem : mAvatarSelectionScrollList->getAllData())
    {
        LLScrollListCell* cell = listItem->getColumn(COL_UUID);
        if (!cell)
            continue;

        LLUUID      selectedAvatarId = cell->getValue().asUUID();
        LLVOAvatar* listAvatar       = getAvatarByUuid(selectedAvatarId);

        if (mPoserAnimator.isPosingAvatar(listAvatar))
            mPoserAnimator.stopPosingAvatar(listAvatar);
    }

    onAvatarSelect();
}

void FSFloaterPoser::onPoseStartStop()
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    bool arePosingSelected = mPoserAnimator.isPosingAvatar(avatar);
    if (arePosingSelected)
    {
        mPoserAnimator.stopPosingAvatar(avatar);
        mPosesWithUnknownSource.erase(avatar->getID());
    }
    else
    {
        if (!couldAnimateAvatar(avatar))
            return;

        if (!havePermissionToAnimateAvatar(avatar))
            return;

        mPosesWithUnknownSource.erase(avatar->getID());
        mPoserAnimator.tryPosingAvatar(avatar);
    }

    onAvatarsRefresh();
    onAvatarSelect();
}

bool FSFloaterPoser::couldAnimateAvatar(LLVOAvatar* avatar) const
{
    if (!avatar || avatar->isDead())
        return false;
    if (avatar->getRegion() != gAgent.getRegion())
        return false;

    return true;
}

bool FSFloaterPoser::havePermissionToAnimateAvatar(LLVOAvatar* avatar) const
{
    if (!avatar || avatar->isDead())
        return false;
    if (avatar->isSelf())
        return true;

    if (avatar->isPuppetAvatar())
    {
        LLVOAvatarPuppet*      control_av     = dynamic_cast<LLVOAvatarPuppet*>(avatar);
        const LLVOVolume*     rootVolume     = control_av->mRootVolp;
        const LLViewerObject* rootEditObject = (rootVolume) ? rootVolume->getRootEdit() : NULL;
        if (!rootEditObject)
            return false;

        return rootEditObject->permYouOwner();
    }

    return false;
}

bool FSFloaterPoser::havePermissionToAnimateOtherAvatar(LLVOAvatar* avatar) const
{
    if (!avatar || avatar->isDead())
        return false;

    return false;
}

void FSFloaterPoser::poseControlsEnable(bool enable)
{
    mTrackballPnl->setEnabled(enable);
    mFlipPoseBtn->setEnabled(enable);
    mFlipJointBtn->setEnabled(enable);
    mRecaptureBtn->setEnabled(enable);
    mTogglePosingBonesBtn->setEnabled(enable);
    mLoadPosesBtn->setEnabled(enable);
    mSavePosesBtn->setEnabled(enable && inventoryExportAllowed());
    mPoseSaveNameEditor->setEnabled(enable);
    mBtnJointReset->setEnabled(enable);
    mRedoChangeBtn->setEnabled(enable);
    mUndoChangeBtn->setEnabled(enable);
    mPositionPnl->setEnabled(enable);
    mMoveTabPnl->setEnabled(enable);
    mTrackballButtonPnl->setEnabled(enable);
}

void FSFloaterPoser::refreshJointScrollListMembers()
{
    mEntireAvJointScroll->clearRows();
    mBodyJointsScrollList->clearRows();
    mFaceJointsScrollList->clearRows();
    mHandJointsScrollList->clearRows();
    mMiscJointsScrollList->clearRows();
    mCollisionVolumesScrollList->clearRows();

    std::vector<FSPoserAnimator::FSPoserJoint>::const_iterator poserJoint_iter;
    for (poserJoint_iter = mPoserAnimator.PoserJoints.begin();
         poserJoint_iter != mPoserAnimator.PoserJoints.end();
         ++poserJoint_iter)
    {
        LLSD row = createRowForJoint(poserJoint_iter->jointName(), false);
        if (!row)
            continue;

        LLScrollListItem *item = nullptr;
        bool hasListHeader = hasString(XML_LIST_HEADER_STRING_PREFIX + poserJoint_iter->jointName());

        switch (poserJoint_iter->boneType())
        {
            case WHOLEAVATAR:
                item = mEntireAvJointScroll->addElement(row);
                mEntireAvJointScroll->selectFirstItem();
                break;

            case BODY:
                if (hasListHeader)
                    addHeaderRowToScrollList(poserJoint_iter->jointName(), mBodyJointsScrollList);

                item = mBodyJointsScrollList->addElement(row);
                break;

            case FACE:
                if (hasListHeader)
                    addHeaderRowToScrollList(poserJoint_iter->jointName(), mFaceJointsScrollList);

                item = mFaceJointsScrollList->addElement(row);
                break;

            case HANDS:
                if (hasListHeader)
                    addHeaderRowToScrollList(poserJoint_iter->jointName(), mHandJointsScrollList);

                item = mHandJointsScrollList->addElement(row);
                break;

            case MISC:
                if (hasListHeader)
                    addHeaderRowToScrollList(poserJoint_iter->jointName(), mMiscJointsScrollList);

                item = mMiscJointsScrollList->addElement(row);
                break;

            case COL_VOLUMES:
                if (hasListHeader)
                    addHeaderRowToScrollList(poserJoint_iter->jointName(), mCollisionVolumesScrollList);

                item = mCollisionVolumesScrollList->addElement(row);
                break;
        }

        if (item)
            item->setUserdata((void*) &*poserJoint_iter);
    }

    refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::addHeaderRowToScrollList(const std::string& jointName, LLScrollListCtrl* bodyJointsScrollList)
{
    LLSD headerRow = createRowForJoint(jointName, true);
    if (!headerRow)
        return;

    LLScrollListItem *hdrRow = bodyJointsScrollList->addElement(headerRow);
    hdrRow->setEnabled(false);
}

LLSD FSFloaterPoser::createRowForJoint(const std::string& jointName, bool isHeaderRow)
{
    if (jointName.empty())
        return {};

    std::string headerValue = "";
    if (isHeaderRow)
        headerValue = tryGetString("icon_category");

    std::string jointValue    = jointName;
    std::string parameterName = (isHeaderRow ? XML_LIST_HEADER_STRING_PREFIX : XML_LIST_TITLE_STRING_PREFIX) + jointName;
    if (hasString(parameterName))
        jointValue = getString(parameterName);
    else
        return {};

    LLSD row;
    row["columns"][COL_ICON]["column"] = "icon";
    row["columns"][COL_ICON]["type"]   = "icon";
    row["columns"][COL_ICON]["value"]  = headerValue;
    row["columns"][COL_NAME]["column"] = "joint";
    row["columns"][COL_NAME]["value"]  = jointValue;

    return row;
}

void FSFloaterPoser::onToggleLoadSavePanel()
{
    if (isMinimized())
        return;

    // Get the load/save button toggle state, find the load/save panel, and set its visibility
    bool loadSavePanelExpanded = mToggleLoadSavePanelBtn->getValue().asBoolean();
    mPosesLoadSavePnl->setVisible(loadSavePanelExpanded);
    mLoadPosesBtn->setVisible(loadSavePanelExpanded);
    mSavePosesBtn->setVisible(loadSavePanelExpanded);
    mBrowserFolderBtn->setVisible(loadSavePanelExpanded);

    // change the width of the Poser panel for the (dis)appearance of the load/save panel
    S32 currentWidth       = getRect().getWidth();
    S32 loadSavePanelWidth = mPosesLoadSavePnl->getRect().getWidth();

    S32 poserFloaterHeight = getRect().getHeight();
    S32 poserFloaterWidth  = loadSavePanelExpanded ? currentWidth + loadSavePanelWidth : currentWidth - loadSavePanelWidth;

    if (poserFloaterWidth < 0)
        return;

    reshape(poserFloaterWidth, poserFloaterHeight);

    if (loadSavePanelExpanded)
        refreshPoseScroll(mPosesScrollList);
}

void FSFloaterPoser::onToggleMirrorChange()
{
    setRotationChangeButtons(true, false);
}

void FSFloaterPoser::onToggleSympatheticChange()
{
    setRotationChangeButtons(false, true);
}

void FSFloaterPoser::setRotationChangeButtons(bool togglingMirror, bool togglingSympathetic)
{
    if (togglingSympathetic)
        mToggleMirrorRotationBtn->setValue(false);

    if (togglingMirror)
        mToggleSympatheticRotationBtn->setValue(false);

    refreshTrackpadCursor();
}

void FSFloaterPoser::onToggleRotationFrameButton(const LLUICtrl* toggleButton)
{
    if (!toggleButton)
        return;

    if (bool buttonDown = toggleButton->getValue().asBoolean())
    {
        mBtnAvatarFrame->setValue(toggleButton == mBtnAvatarFrame);
        mBtnScreenFrame->setValue(toggleButton == mBtnScreenFrame);
        mBtnWorldFrame->setValue(toggleButton == mBtnWorldFrame);
    }

    FSToolCompPose::getInstance()->setReferenceFrame(getReferenceFrame());
    FSToolCompPoseTranslate::getInstance()->setReferenceFrame(getReferenceFrame());
    refreshRotationSlidersAndSpinners();
}

void FSFloaterPoser::onUndoLastChange()
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (currentlyPosing)
            mPoserAnimator.undoLastJointChange(avatar, *item, getUiSelectedBoneDeflectionStyle());
    }

    enableOrDisableRedoAndUndoButton();
    refreshRotationSlidersAndSpinners();
    refreshPositionSlidersAndSpinners();
    refreshScaleSlidersAndSpinners();
    refreshTrackpadCursor();
    if (getSavingToBvh())
        refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::onSetAvatarToTpose()
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    setSavePosesButtonText(false);
    mPoserAnimator.setAllAvatarStartingRotationsToZero(avatar);
    refreshTextHighlightingOnJointScrollLists();
    enableOrDisableRedoAndUndoButton();
}

void FSFloaterPoser::onResetJoint(const LLSD& data)
{
    int resetType = data.asInteger();

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosing)
            continue;

        mPoserAnimator.resetJoint(avatar, *item, getUiSelectedBoneDeflectionStyle());
    }

    refreshRotationSlidersAndSpinners();
    refreshPositionSlidersAndSpinners();
    refreshScaleSlidersAndSpinners();
    refreshTrackpadCursor();
    enableOrDisableRedoAndUndoButton();
    refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::onRedoLastChange()
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (currentlyPosing)
            mPoserAnimator.redoLastJointChange(avatar, *item, getUiSelectedBoneDeflectionStyle());
    }

    enableOrDisableRedoAndUndoButton();
    refreshRotationSlidersAndSpinners();
    refreshTrackpadCursor();
    refreshScaleSlidersAndSpinners();
    refreshPositionSlidersAndSpinners();
    if (getSavingToBvh())
        refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::enableOrDisableRedoAndUndoButton()
{
    mRedoChangeBtn->setEnabled(false);
    mUndoChangeBtn->setEnabled(false);

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    bool shouldEnableRedoButton = false;
    bool shouldEnableUndoButton = false;

    for (auto item : selectedJoints)
    {
        bool currentlyPosing = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosing)
            continue;

        shouldEnableRedoButton |= mPoserAnimator.canRedoOrUndoJointChange(avatar, *item);
        shouldEnableUndoButton |= mPoserAnimator.canRedoOrUndoJointChange(avatar, *item, true);
    }

    mRedoChangeBtn->setEnabled(shouldEnableRedoButton);
    mUndoChangeBtn->setEnabled(shouldEnableUndoButton);
}

void FSFloaterPoser::onToggleVisualManipulators()
{
    bool tools_enabled = mToggleVisualManipulators->getValue().asBoolean();

    if (tools_enabled)
    {
        enableVisualManipulators();
    }
    else
    {
        disableVisualManipulators();
    }
}

void FSFloaterPoser::selectJointByName(const std::string& jointName)
{
    LLTabContainer* tabContainer = mJointsTabs;
    std::vector<LLPanel*> panels = {
        mPositionRotationPnl,
        mBodyJointsPnl,
        mFaceJointsPnl,
        mHandsTabs,
        mMiscJointsPnl,
        mCollisionVolumesPnl
    };
    
    std::vector<LLScrollListCtrl*> scrollLists = {
        mEntireAvJointScroll,
        mBodyJointsScrollList,
        mFaceJointsScrollList,
        mHandJointsScrollList,
        mMiscJointsScrollList,
        mCollisionVolumesScrollList
    };

    bool found = false;
    for (S32 i = 0; i < tabContainer->getTabCount(); ++i)
    {
        LLPanel* panel = tabContainer->getPanelByIndex(i);
        tabContainer->selectTabPanel(panel);

        // Special handling for Hands tab
        if (panel == mHandsTabs)
        {
            mHandsTabs->selectTabPanel(mHandsJointsPnl);
        }

        for (auto scrollList : scrollLists)
        {
            scrollList->deselectAllItems();
        }

        auto scrollList = getScrollListForTab(panel);

        std::vector<LLScrollListItem*> items = scrollList->getAllData();
        for (auto item : items)
        {
            auto* userData = static_cast<FSPoserAnimator::FSPoserJoint*>(item->getUserdata());
            if (userData && userData->jointName() == jointName)
            {
                tabContainer->selectTab(i);
                scrollList->selectNthItem(scrollList->getItemIndex(item));
                scrollList->scrollToShowSelected();
                getUiSelectedPoserJoints();
                return; // Exit the loop once we've found and selected the joint
            }
        }
    }
    llwarns << "Joint not found: " << jointName << llendl;
}

LLScrollListCtrl* FSFloaterPoser::getScrollListForTab(LLPanel* tabPanel) const
{
    if (tabPanel == mPositionRotationPnl)
    {
        return mEntireAvJointScroll;
    }
    else if (tabPanel == mBodyJointsPnl)
    {
        return mBodyJointsScrollList;
    }
    else if (tabPanel == mFaceJointsPnl)
    {
        return mFaceJointsScrollList;
    }
    else if (tabPanel == mHandsTabs)
    {
        return mHandJointsScrollList;
    }
    else if (tabPanel == mMiscJointsPnl)
    {
        return mMiscJointsScrollList;
    }
    else if (tabPanel == mCollisionVolumesPnl)
    {
        return mCollisionVolumesScrollList;
    }

    return nullptr;
}

std::vector<FSPoserAnimator::FSPoserJoint*> FSFloaterPoser::getUiSelectedPoserJoints() const
{
    std::vector<FSPoserAnimator::FSPoserJoint*> joints;

    auto activeTab = mJointsTabs->getCurrentPanel();
    if (!activeTab)
    {
        return joints;
    }

    LLScrollListCtrl* scrollList{ nullptr };
    
    scrollList = getScrollListForTab(activeTab);
    if (activeTab == mPositionRotationPnl)
    {
        mEntireAvJointScroll->selectFirstItem();
    }
    else if (activeTab == mHandsTabs )
    {
        auto activeHandsSubTab = mHandsTabs->getCurrentPanel();
        if (!activeHandsSubTab)
        {
            return joints;
        }
    }
    if (!scrollList)
    {
        return joints;
    }

    for (auto item : scrollList->getAllSelected())
    {
        if (auto* userData = static_cast<FSPoserAnimator::FSPoserJoint*>(item->getUserdata()))
        {
            joints.push_back(userData);
        }
    }

    updateManipWithFirstSelectedJoint(joints);

    return joints;
}

void FSFloaterPoser::updateManipWithFirstSelectedJoint(const std::vector<FSPoserAnimator::FSPoserJoint*>& joints) const
{
    auto avatarp = getUiSelectedAvatar();
    if (!avatarp)
        return;

    bool haveImplicitPermission = havePermissionToAnimateAvatar(avatarp);
    bool iCanPoseThem           = havePermissionToAnimateOtherAvatar(avatarp);

    if ((joints.size() >= 1) && (haveImplicitPermission || iCanPoseThem))
    {
        FSToolCompPose::getInstance()->setJoint(avatarp->getRootJoint()->findJoint(joints[0]->jointName()));
        FSToolCompPoseTranslate::getInstance()->setJoint(avatarp->getRootJoint()->findJoint(joints[0]->jointName()));
    }
    else
    {
        FSToolCompPose::getInstance()->setJoint(nullptr);
        FSToolCompPoseTranslate::getInstance()->setJoint(nullptr);
    }
}

E_RotationStyle FSFloaterPoser::getUiSelectedBoneRotationStyle(const std::string& jointName) const
{
    if (jointName.empty())
        return ABSOLUTE_ROT;

    bool hasRotationStylePreferenceParameter = hasString(XML_JOINT_DELTAROT_STRING_PREFIX + jointName);
    if (!hasRotationStylePreferenceParameter)
        return ABSOLUTE_ROT;
 
    std::string paramValue = getString(XML_JOINT_DELTAROT_STRING_PREFIX + jointName);
    if (paramValue == "true")
        return DELTAIC_ROT;

    return ABSOLUTE_ROT;
}

E_BoneDeflectionStyles FSFloaterPoser::getUiSelectedBoneDeflectionStyle() const
{
    bool isDelta = mToggleDeltaModeBtn->getValue().asBoolean();

    if (mToggleMirrorRotationBtn->getValue().asBoolean())
        return isDelta ? MIRROR_DELTA : MIRROR;

    if (mToggleSympatheticRotationBtn->getValue().asBoolean())
        return isDelta ? SYMPATHETIC_DELTA : SYMPATHETIC;

    return isDelta ? DELTAMODE : NONE;
}

LLVOAvatar* FSFloaterPoser::getUiSelectedAvatar() const
{
    LLScrollListItem* item = mAvatarSelectionScrollList->getFirstSelected();
    if (!item)
        return nullptr;

    LLScrollListCell* cell = item->getColumn(COL_UUID);
    if (!cell)
        return nullptr;

    LLUUID selectedAvatarId = cell->getValue().asUUID();

    return getAvatarByUuid(selectedAvatarId);
}

void FSFloaterPoser::setUiSelectedAvatar(const LLUUID& avatarToSelect)
{
    for (auto listItem : mAvatarSelectionScrollList->getAllData())
    {
        LLScrollListCell* cell = listItem->getColumn(COL_UUID);
        if (!cell)
            continue;

        LLUUID avatarId = cell->getValue().asUUID();
        if (avatarId != avatarToSelect)
            continue;

        listItem->setSelected(true);
        break;
    }
}

void FSFloaterPoser::setPoseSaveFileTextBoxToUiSelectedAvatarSaveFileName()
{
    LLScrollListItem* item = mAvatarSelectionScrollList->getFirstSelected();
    if (!item)
        return;

    LLScrollListCell* cell = item->getColumn(COL_SAVE);
    if (!cell)
        return;

    std::string lastSetName = cell->getValue().asString();
    if (lastSetName.empty())
        return;

    std::string name = std::string(lastSetName);
    mPoseSaveNameEditor->setText(name);
}

void FSFloaterPoser::setUiSelectedAvatarSaveFileName(const std::string& saveFileName)
{
    LLScrollListItem* item = mAvatarSelectionScrollList->getFirstSelected();
    if (!item)
        return;

    LLScrollListCell* cell = item->getColumn(COL_SAVE);
    if (!cell)
        return;

    return cell->setValue(saveFileName);
}

LLVOAvatar* FSFloaterPoser::getAvatarByUuid(const LLUUID& avatarToFind) const
{
    if (avatarToFind.isNull())
        return nullptr;

    for (LLCharacter* character : LLCharacter::sInstances)
    {
        if (avatarToFind != character->getID())
            continue;

        LLVOAvatar* avatar = dynamic_cast<LLVOAvatar*>(character);
        return avatar;
    }

    return nullptr;
}

void FSFloaterPoser::onPositionSet()
{
    F32 posX = (F32)mAdvPosXSpnr->getValue().asReal();
    F32 posY = (F32)mAdvPosYSpnr->getValue().asReal();
    F32 posZ = (F32)mAdvPosZSpnr->getValue().asReal();
    
    mInOutSpnr->setValue(posX);
    mLeftRightSpnr->setValue(posY);
    mUpDownSpnr->setValue(posZ);
    mAdvPosXSlider->setValue(posX);
    mAdvPosYSlider->setValue(posY);
    mAdvPosZSlider->setValue(posZ);
    mPosXSlider->setValue(posX);
    mPosYSlider->setValue(posY);
    mPosZSlider->setValue(posZ);

    setSelectedJointsPosition(posX, posY, posZ);
    refreshPositionSlidersAndSpinners();
    enableOrDisableRedoAndUndoButton();
}

void FSFloaterPoser::onScaleSet()
{
    F32 scX = (F32)mScaleXSpnr->getValue().asReal();
    F32 scY = (F32)mScaleYSpnr->getValue().asReal();
    F32 scZ = (F32)mScaleZSpnr->getValue().asReal();

    mAdvScaleXSlider->setValue(scX);
    mAdvScaleYSlider->setValue(scY);
    mAdvScaleZSlider->setValue(scZ);

    setSelectedJointsScale(scX, scY, scZ);
    refreshScaleSlidersAndSpinners();
    enableOrDisableRedoAndUndoButton();
}

void FSFloaterPoser::onTrackballChanged()
{
    LLVector3 trackPadDeltaPos;
    LLSD      deltaPosition = mAvatarTrackball->getValueDelta();

    if (deltaPosition.isArray() && deltaPosition.size() == 3)
        trackPadDeltaPos.setValue(deltaPosition);
    else
        return;

    F32 trackPadSensitivity = llmax(gSavedSettings.getF32(std::string(POSER_TRACKPAD_SENSITIVITY_SAVE_KEY).c_str()), 0.0001f);

    trackPadDeltaPos[VX] *= NormalTrackpadRangeInRads * trackPadSensitivity * RAD_TO_DEG;
    trackPadDeltaPos[VY] *= NormalTrackpadRangeInRads * trackPadSensitivity * RAD_TO_DEG;
    trackPadDeltaPos[VZ] *= NormalTrackpadRangeInRads * RAD_TO_DEG;

    F32 axis1 = clipRange((F32)mYawSpnr->getValue().asReal() + trackPadDeltaPos[VX]);
    F32 axis2 = (F32)mPitchSpnr->getValue().asReal() + trackPadDeltaPos[VY];
    F32 axis3 = (F32)mRollSpnr->getValue().asReal() + trackPadDeltaPos[VZ];

    mYawSpnr->setValue(axis1);
    mPitchSpnr->setValue(axis2);
    mRollSpnr->setValue(axis3);

    onYawPitchRollChanged(true);
}

F32 FSFloaterPoser::clipRange(F32 value)
{
    F32 result = fmodf(value, 3600.f); // to avoid time consuming while loops
    while (result > 180.f)
        result -= 360.f;
    while (result < -180.f)
        result += 360.f;

    return result;
}

void FSFloaterPoser::onYawPitchRollChanged(bool skipUpdateTrackpad)
{
    LLVector3 absoluteRotation, deltaRotation;
    absoluteRotation.mV[VX] = (F32)mYawSpnr->getValue().asReal();
    absoluteRotation.mV[VY] = (F32)mPitchSpnr->getValue().asReal();
    absoluteRotation.mV[VZ] = (F32)mRollSpnr->getValue().asReal();

    mAdvRotXSlider->setValue(absoluteRotation.mV[VY]);
    mAdvRotYSlider->setValue(absoluteRotation.mV[VX]);
    mAdvRotZSlider->setValue(absoluteRotation.mV[VZ]);

    absoluteRotation *= DEG_TO_RAD;
    deltaRotation = absoluteRotation - mLastSliderRotation;
    mLastSliderRotation = absoluteRotation;

    setSelectedJointsRotation(absoluteRotation, deltaRotation);
    enableOrDisableRedoAndUndoButton();

    if (!skipUpdateTrackpad)
        refreshTrackpadCursor();
}

void FSFloaterPoser::onAdjustTrackpadSensitivity()
{
    refreshTrackpadCursor();
}

void FSFloaterPoser::refreshTrackpadCursor()
{
    F32 trackPadSensitivity = llmax(gSavedSettings.getF32(std::string(POSER_TRACKPAD_SENSITIVITY_SAVE_KEY).c_str()), 0.0001f);
    F32 axis1 = (F32)mYawSpnr->getValue().asReal() * DEG_TO_RAD / NormalTrackpadRangeInRads / trackPadSensitivity;
    F32 axis2 = (F32)mPitchSpnr->getValue().asReal() * DEG_TO_RAD / NormalTrackpadRangeInRads / trackPadSensitivity;
    F32 axis3 = (F32)mRollSpnr->getValue().asReal() * DEG_TO_RAD / NormalTrackpadRangeInRads;

    mAvatarTrackball->setValue(axis1, axis2, axis3);
}

void FSFloaterPoser::refreshRotationSlidersAndSpinners()
{
    LLVector3 rotation = getRotationOfFirstSelectedJoint();

    mLastSliderRotation = rotation;
    mYawSpnr->setValue(rotation.mV[VX] *= RAD_TO_DEG);
    mPitchSpnr->setValue(rotation.mV[VY] *= RAD_TO_DEG);
    mRollSpnr->setValue(rotation.mV[VZ] *= RAD_TO_DEG);
    mAdvRotXSlider->setValue(rotation.mV[VY]);
    mAdvRotYSlider->setValue(rotation.mV[VX]);
    mAdvRotZSlider->setValue(rotation.mV[VZ]);
}

void FSFloaterPoser::refreshPositionSlidersAndSpinners()
{
    LLVector3 position = getPositionOfFirstSelectedJoint();

    mAdvPosXSlider->setValue(position.mV[VX]);
    mAdvPosXSpnr->setValue(position.mV[VX]);
    mAdvPosYSlider->setValue(position.mV[VY]);
    mAdvPosYSpnr->setValue(position.mV[VY]);
    mAdvPosZSlider->setValue(position.mV[VZ]);
    mAdvPosZSpnr->setValue(position.mV[VZ]);

    auto activeTab = mJointsTabs->getCurrentPanel();
    if (!activeTab)
        return;

    if (activeTab != mPositionRotationPnl)
        return; // if the active tab isn't the av position one, don't set anything.

    mPosXSlider->setValue(position.mV[VX]);
    mInOutSpnr->setValue(position.mV[VX]);
    mPosYSlider->setValue(position.mV[VY]);
    mLeftRightSpnr->setValue(position.mV[VY]);
    mPosZSlider->setValue(position.mV[VZ]);
    mUpDownSpnr->setValue(position.mV[VZ]);
}

void FSFloaterPoser::refreshScaleSlidersAndSpinners()
{
    LLVector3 rotation = getScaleOfFirstSelectedJoint();

    mAdvScaleXSlider->setValue(rotation.mV[VX]);
    mScaleXSpnr->setValue(rotation.mV[VX]);
    mAdvScaleYSlider->setValue(rotation.mV[VY]);
    mScaleYSpnr->setValue(rotation.mV[VY]);
    mAdvScaleZSlider->setValue(rotation.mV[VZ]);
    mScaleZSpnr->setValue(rotation.mV[VZ]);
}

void FSFloaterPoser::setSelectedJointsPosition(F32 x, F32 y, F32 z)
{
    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    LLVector3              vec3  = LLVector3(x, y, z);
    E_BoneDeflectionStyles defl  = getUiSelectedBoneDeflectionStyle();
    E_PoserReferenceFrame  frame = getReferenceFrame();

    for (auto item : getUiSelectedPoserJoints())
    {
        bool currentlyPosingJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosingJoint)
            continue;

        mPoserAnimator.setJointPosition(avatar, item, vec3, frame, defl);
    }
}

void FSFloaterPoser::setSelectedJointsRotation(const LLVector3& absoluteRot, const LLVector3& deltaRot)
{
    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    auto                   selectedJoints   = getUiSelectedPoserJoints();
    bool                   savingToExternal = getSavingToBvh();
    E_BoneDeflectionStyles deflection       = getUiSelectedBoneDeflectionStyle();
    E_PoserReferenceFrame  frame            = getReferenceFrame();

    for (auto item : selectedJoints)
    {
        bool currentlyPosingJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosingJoint)
            continue;

        auto oppositeJoint = mPoserAnimator.getPoserJointByName(item->mirrorJointName());
        if (oppositeJoint)
        {
            bool oppositeJointAlsoSelectedOnUi =
                std::find(selectedJoints.begin(), selectedJoints.end(), oppositeJoint) != selectedJoints.end();

            bool deflectionDoesOppositeLimbs = !(deflection == NONE || deflection == DELTAMODE);
            if (oppositeJointAlsoSelectedOnUi && deflectionDoesOppositeLimbs && item->dontFlipOnMirror())
                continue;
        }

        S32                   jointNegation = getJointNegation(frame, item->jointName());
        E_BoneAxisTranslation translation   = getJointTranslation(frame, item->jointName());
        E_RotationStyle       style         = getUiSelectedBoneRotationStyle(item->jointName());

        mPoserAnimator.setJointRotation(avatar, item, absoluteRot, deltaRot, deflection, frame, translation, jointNegation,
                                        savingToExternal, style);
    }

    if (savingToExternal)
        refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::setSelectedJointsScale(F32 x, F32 y, F32 z)
{
    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    LLVector3              vec3  = LLVector3(x, y, z);
    E_BoneDeflectionStyles defl  = getUiSelectedBoneDeflectionStyle();
    E_PoserReferenceFrame  frame = getReferenceFrame();

    for (auto item : getUiSelectedPoserJoints())
    {
        bool currentlyPosingJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosingJoint)
            continue;

        mPoserAnimator.setJointScale(avatar, item, vec3, frame, defl);
    }
}

LLVector3 FSFloaterPoser::getRotationOfFirstSelectedJoint() const
{
    LLVector3 rotation;
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return rotation;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return rotation;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return rotation;

    E_PoserReferenceFrame frame = getReferenceFrame();

    rotation = mPoserAnimator.getJointRotation(avatar, *selectedJoints.front(), getJointTranslation(frame, selectedJoints.front()->jointName()),
                                        getJointNegation(frame, selectedJoints.front()->jointName()));

    return rotation;
}

LLVector3 FSFloaterPoser::getPositionOfFirstSelectedJoint() const
{
    LLVector3 position;
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return position;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return position;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return position;

    position = mPoserAnimator.getJointPosition(avatar, *selectedJoints.front());
    return position;
}

LLVector3 FSFloaterPoser::getScaleOfFirstSelectedJoint() const
{
    LLVector3 scale;
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return scale;

    LLVOAvatar *avatar = getUiSelectedAvatar();
    if (!avatar)
        return scale;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return scale;

    scale = mPoserAnimator.getJointScale(avatar, *selectedJoints.front());
    return scale;
}

void FSFloaterPoser::onJointTabSelect()
{
    refreshPositionSlidersAndSpinners();
    refreshRotationSlidersAndSpinners(); 
    refreshTrackpadCursor();
    enableOrDisableRedoAndUndoButton();
    refreshScaleSlidersAndSpinners();
    markSelectedJointsToHighlight();
}

E_BoneAxisTranslation FSFloaterPoser::getJointTranslation(E_PoserReferenceFrame frame, const std::string& jointName) const
{
    if (jointName.empty())
        return SWAP_NOTHING;

    std::string paramName;
    if (frame == POSER_FRAME_BONE)
        paramName = XML_JOINT_TRANSFORM_STRING_PREFIX + jointName;
    else
        paramName = XML_JOINT_FRAME_TRANSFORM_PREFIX + jointName;

    bool hasTransformParameter = hasString(paramName);
    if (!hasTransformParameter)
        return SWAP_NOTHING;

    std::string paramValue = getString(paramName);

    if (strstr(paramValue.c_str(), "SWAP_YAW_AND_ROLL"))
        return SWAP_YAW_AND_ROLL;
    else if (strstr(paramValue.c_str(), "SWAP_YAW_AND_PITCH"))
        return SWAP_YAW_AND_PITCH;
    else if (strstr(paramValue.c_str(), "SWAP_ROLL_AND_PITCH"))
        return SWAP_ROLL_AND_PITCH;
    else if (strstr(paramValue.c_str(), "SWAP_X2Y_Y2Z_Z2X"))
        return SWAP_X2Y_Y2Z_Z2X;
    else if (strstr(paramValue.c_str(), "SWAP_X2Z_Y2X_Z2Y"))
        return SWAP_X2Z_Y2X_Z2Y;
    else
        return SWAP_NOTHING;
}

S32 FSFloaterPoser::getJointNegation(E_PoserReferenceFrame frame, const std::string& jointName) const
{
    S32 result = NEGATE_NOTHING;

    if (jointName.empty())
        return result;

    std::string paramName;
    if (frame == POSER_FRAME_BONE)
        paramName = XML_JOINT_TRANSFORM_STRING_PREFIX + jointName;
    else
        paramName = XML_JOINT_FRAME_TRANSFORM_PREFIX + jointName;

    bool hasNegationParameter = hasString(paramName);
    if (!hasNegationParameter)
        return result;

    std::string paramValue = getString(paramName);

    if (strstr(paramValue.c_str(), "NEGATE_YAW"))
        result |= NEGATE_YAW;
    if (strstr(paramValue.c_str(), "NEGATE_PITCH"))
        result |= NEGATE_PITCH;
    if (strstr(paramValue.c_str(), "NEGATE_ROLL"))
        result |= NEGATE_ROLL;
    if (strstr(paramValue.c_str(), "NEGATE_ALL"))
        return NEGATE_ALL;

    return result;
}

E_PoserReferenceFrame FSFloaterPoser::getReferenceFrame() const
{
    bool toggleButtonValue = mBtnScreenFrame->getValue().asBoolean();
    if (toggleButtonValue)
        return POSER_FRAME_CAMERA;

    toggleButtonValue = mBtnAvatarFrame->getValue().asBoolean();
    if (toggleButtonValue)
        return POSER_FRAME_AVATAR;

    toggleButtonValue = mBtnWorldFrame->getValue().asBoolean();
    if (toggleButtonValue)
        return POSER_FRAME_WORLD;

    return POSER_FRAME_BONE;
}

/// <summary>
/// An event handler for selecting an avatar or animesh on the POSES_AVATAR_SCROLL_LIST_NAME.
/// In general this will refresh the views for joints or their proxies, and (dis/en)able elements of the view.
/// </summary>
void FSFloaterPoser::onAvatarSelect()
{
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    bool          isSelf                 = avatar->isSelf();
    bool          haveImplicitPermission = havePermissionToAnimateAvatar(avatar); // self & control avatars you own
    bool          haveExplicitPermission = havePermissionToAnimateOtherAvatar(avatar); // as permissions allow

    if (haveImplicitPermission || haveExplicitPermission)
    {
        FSToolCompPose::getInstance()->setAvatar(avatar);
        FSToolCompPoseTranslate::getInstance()->setAvatar(avatar);
    }
    else
    {
        FSToolCompPose::getInstance()->setAvatar(nullptr);
        FSToolCompPoseTranslate::getInstance()->setAvatar(nullptr);
    }

    bool arePosingSelected = mPoserAnimator.isPosingAvatar(avatar);

    mStartStopPosingBtn->setEnabled(haveImplicitPermission);
    mStartStopPosingBtn->setValue(arePosingSelected);

    mSetToTposeButton->setEnabled(haveImplicitPermission && arePosingSelected);
    poseControlsEnable(arePosingSelected && haveImplicitPermission);

    refreshTextHighlightingOnAvatarScrollList();
    refreshTextHighlightingOnJointScrollLists();
    onJointTabSelect();
    setPoseSaveFileTextBoxToUiSelectedAvatarSaveFileName();
}

uuid_vec_t FSFloaterPoser::getNearbyAvatarsAndAnimeshes() const
{
    uuid_vec_t avatar_ids;

    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLVOAvatar* avatar = dynamic_cast<LLVOAvatar*>(character);
        if (!avatarIsNearbyMe(avatar))
            continue;

        bool isMuted = LLMuteList::isMuted(avatar->getID());
        if (isMuted)
            continue;

        bool isSelfOrCtrl = avatar->isPuppetAvatar() || avatar->isSelf();

        if (!isSelfOrCtrl)
            continue;

        avatar_ids.emplace_back(character->getID());
    }

    return avatar_ids;
}

bool FSFloaterPoser::avatarIsNearbyMe(LLCharacter* character) const
{
    if (!gAgentAvatarp || gAgentAvatarp.isNull() || !character)
        return false;

    LLVector3 separationVector = character->getCharacterPosition() - gAgentAvatarp->getCharacterPosition();

    return separationVector.length() < 50.f;
}

uuid_vec_t FSFloaterPoser::getCurrentlyListedAvatarsAndAnimeshes() const
{
    uuid_vec_t avatar_ids;

    for (auto listItem : mAvatarSelectionScrollList->getAllData())
    {
        LLScrollListCell* cell = listItem->getColumn(COL_UUID);
        if (!cell)
            continue;

        LLUUID avatarId = cell->getValue().asUUID();
        avatar_ids.emplace_back(avatarId);
    }

    return avatar_ids;
}

S32 FSFloaterPoser::getAvatarListIndexForUuid(const LLUUID& toFind) const
{
    S32 result = -1;
    for (auto listItem : mAvatarSelectionScrollList->getAllData())
    {
        result++;

        LLScrollListCell* cell = listItem->getColumn(COL_UUID);
        if (!cell)
            continue;

        LLUUID avatarId = cell->getValue().asUUID();
        if (avatarId == toFind)
            return result;
    }

    return -1;
}

void FSFloaterPoser::onAvatarsRefresh()
{
    uuid_vec_t avatarsToAddToList, avatarsToRemoveFromList;
    uuid_vec_t nearbyAvatarIds = getNearbyAvatarsAndAnimeshes();
    uuid_vec_t currentlyListedAvatars = getCurrentlyListedAvatarsAndAnimeshes();
    LLCommonUtils::computeDifference(nearbyAvatarIds, currentlyListedAvatars, avatarsToAddToList, avatarsToRemoveFromList);

    for (LLUUID toRemove : avatarsToRemoveFromList)
    {
        S32 indexToRemove = getAvatarListIndexForUuid(toRemove);
        if (indexToRemove >= 0)
            mAvatarSelectionScrollList->deleteSingleItem(indexToRemove);
    }

    std::string iconObjectName = "Inv_Object";
    if (hasString("icon_object"))
        iconObjectName = getString("icon_object");

    // Add non-Animesh avatars
    for (LLCharacter* character : LLCharacter::sInstances)
    {
        LLUUID uuid = character->getID();
        if (std::find(avatarsToAddToList.begin(), avatarsToAddToList.end(), uuid) == avatarsToAddToList.end())
            continue;

        LLVOAvatar* avatar = dynamic_cast<LLVOAvatar*>(character);
        if (!couldAnimateAvatar(avatar))
            continue;

        if (avatar->isPuppetAvatar())
            continue;

        LLAvatarName av_name;
        if (!LLAvatarNameCache::get(uuid, &av_name))
            continue;

        if (LLMuteList::isMuted(uuid))
            continue;

        if (!avatar->isSelf())
            continue;

        LLSD row;
        row["columns"][COL_ICON]["column"] = "icon";
        row["columns"][COL_ICON]["type"]   = "icon";
        row["columns"][COL_ICON]["value"]  = getIconNameForAvatar(avatar);
        row["columns"][COL_NAME]["column"] = "name";
        row["columns"][COL_NAME]["value"]  = av_name.mDisplayName;
        row["columns"][COL_UUID]["column"] = "uuid";
        row["columns"][COL_UUID]["value"]  = uuid;
        row["columns"][COL_SAVE]["column"] = "saveFileName";
        row["columns"][COL_SAVE]["value"]  = "";
        LLScrollListItem* item             = mAvatarSelectionScrollList->addElement(row);
    }

    // Add Animesh avatars
    for (auto character : LLCharacter::sInstances)
    {
        LLUUID uuid = character->getID();
        if (std::find(avatarsToAddToList.begin(), avatarsToAddToList.end(), uuid) == avatarsToAddToList.end())
            continue;

        LLVOAvatarPuppet* avatar = dynamic_cast<LLVOAvatarPuppet* >(character);
        if (!couldAnimateAvatar(avatar))
            continue;

        LLAvatarName av_name;
        std::string animeshName = getControlAvatarName(avatar);
        if (animeshName.empty())
            continue;

        LLSD row;
        row["columns"][COL_ICON]["column"] = "icon";
        row["columns"][COL_ICON]["type"]   = "icon";
        row["columns"][COL_ICON]["value"]  = iconObjectName;
        row["columns"][COL_NAME]["column"] = "name";
        row["columns"][COL_NAME]["value"]  = animeshName;
        row["columns"][COL_UUID]["column"] = "uuid";
        row["columns"][COL_UUID]["value"]  = avatar->getID();
        row["columns"][COL_SAVE]["column"] = "saveFileName";
        row["columns"][COL_SAVE]["value"]  = "";
        mAvatarSelectionScrollList->addElement(row);
    }

    mAvatarSelectionScrollList->updateLayout();
    refreshTextHighlightingOnAvatarScrollList();
}

std::string FSFloaterPoser::getIconNameForAvatar(LLVOAvatar* avatar)
{
    std::string iconName = "Inv_BodyShape";
    if (hasString("icon_category"))
        iconName = getString("icon_category");

    if (!avatar)
        return iconName;

    if (avatar->isPuppetAvatar() && hasString("icon_object"))
        return getString("icon_object");

    return iconName;
}

std::string FSFloaterPoser::getControlAvatarName(const LLVOAvatarPuppet* avatar)
{
    if (!avatar)
        return {};

    const LLVOVolume* rootVolume = avatar->mRootVolp;
    const LLViewerObject* rootEditObject = rootVolume ? rootVolume->getRootEdit() : nullptr;
    if (!rootEditObject)
        return {};

    const LLViewerInventoryItem* attachedItem =
        (rootEditObject->isAttachment()) ? gInventory.getItem(rootEditObject->getAttachmentItemID()) : nullptr;

    if (attachedItem)
        return attachedItem->getName();

    if (rootEditObject->permYouOwner())
        return const_cast<LLVOAvatarPuppet*>(avatar)->getFullname();

    return {};
}

void FSFloaterPoser::refreshTextHighlightingOnAvatarScrollList()
{
    for (auto listItem : mAvatarSelectionScrollList->getAllData())
    {
        LLScrollListCell* cell = listItem->getColumn(COL_UUID);
        if (!cell)
            continue;

        LLUUID selectedAvatarId = cell->getValue().asUUID();
        LLVOAvatar* listAvatar = getAvatarByUuid(selectedAvatarId);

        ((LLScrollListText*)listItem->getColumn(COL_ICON))->setValue(getIconNameForAvatar(listAvatar));

        if (mPoserAnimator.isPosingAvatar(listAvatar))
            ((LLScrollListText*)listItem->getColumn(COL_NAME))->setFontStyle(LLFontGL::BOLD);
        else
            ((LLScrollListText*)listItem->getColumn(COL_NAME))->setFontStyle(LLFontGL::NORMAL);
    }
}

void FSFloaterPoser::refreshTextHighlightingOnJointScrollLists()
{
    LLVOAvatar *avatar = getUiSelectedAvatar();

    addBoldToScrollList(mBodyJointsScrollList, avatar);
    addBoldToScrollList(mFaceJointsScrollList, avatar);
    addBoldToScrollList(mHandJointsScrollList, avatar);
    addBoldToScrollList(mMiscJointsScrollList, avatar);
    addBoldToScrollList(mCollisionVolumesScrollList, avatar);
}

void FSFloaterPoser::setSavePosesButtonText(bool setAsSaveDiff)
{
    setAsSaveDiff ? mSavePosesBtn->setLabel(tryGetString("SaveDiffLabel")) : mSavePosesBtn->setLabel(tryGetString("SavePoseLabel"));
}

void FSFloaterPoser::addBoldToScrollList(LLScrollListCtrl* list, LLVOAvatar* avatar)
{
    if (!avatar)
        return;

    if (!list)
        return;

    std::string iconValue                    = "";
    bool        considerExternalFormatSaving = getSavingToBvh();

    for (auto listItem : list->getAllData())
    {
        FSPoserAnimator::FSPoserJoint* poserJoint = static_cast<FSPoserAnimator::FSPoserJoint*>(listItem->getUserdata());
        if (!poserJoint)
            continue;

        ((LLScrollListText*)listItem->getColumn(COL_ICON))->setValue(getScrollListIconForJoint(avatar, *poserJoint));

        if (mPoserAnimator.isPosingAvatarJoint(avatar, *poserJoint))
            ((LLScrollListText*)listItem->getColumn(COL_NAME))->setFontStyle(LLFontGL::BOLD);
        else
            ((LLScrollListText*)listItem->getColumn(COL_NAME))->setFontStyle(LLFontGL::NORMAL);
    }
}

std::string FSFloaterPoser::getScrollListIconForJoint(LLVOAvatar* avatar, FSPoserAnimator::FSPoserJoint joint)
{
    if (!avatar)
        return {};

    if (mPoserAnimator.getRotationIsWorldLocked(avatar, joint))
        return tryGetString("icon_rotation_is_world_locked");

    if (!getSavingToBvh())
        return {};

    if (joint.boneType() == COL_VOLUMES)
        return tryGetString("icon_rotation_does_not_export");

    if (mPoserAnimator.userSetBaseRotationToZero(avatar, joint))
    {
        if (mPoserAnimator.exportRotationWillLockJoint(avatar, joint))
            return tryGetString("icon_rotation_bvh_locked_edited");
        else
            return tryGetString("icon_rotation_bvh_locked_unedited");
    }
    else
        return tryGetString("icon_rotation_bvh_unlocked");
}

std::string FSFloaterPoser::tryGetString(std::string_view name)
{
    if (name.empty())
        return {};

    return hasString(name) ? getString(std::string(name)) : "";
}

bool FSFloaterPoser::savePoseToBvh(LLVOAvatar* avatar, const std::string& poseFileName)
{
    if (poseFileName.empty())
        return false;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return false;

    if (!inventoryExportAllowed() || mPosesWithUnknownSource.count(avatar->getID()))
        return false;  // Do not export imported poses of unverified authorship.

    bool writeSuccess = false;

    try
    {
        std::string pathname = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
        if (!FSPoserFilePaths::fileExists(pathname))
        {
            llwarns << "Couldn't find folder: " << pathname << " - creating one." << llendl;
            LLFile::mkdir(pathname);
        }

        std::string fullSavePath =
            FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, poseFileName + POSE_EXTERNAL_FORMAT_FILE_EXT);

        llofstream file(fullSavePath.c_str());
        if (!file.is_open())
        {
            llwarns << "Unable to save pose!" << llendl;
            return false;
        }

        writeSuccess = writePoseAsBvh(&file, avatar);

        file.close();
    }
    catch (const std::exception& e)
    {
        llwarns << "Exception caught in SaveToBVH: " << e.what() << llendl;
        return false;
    }

    return writeSuccess;
}

bool FSFloaterPoser::writePoseAsBvh(llofstream* fileStream, LLVOAvatar* avatar)
{
    if (!fileStream || !avatar)
        return false;

    *fileStream << "HIERARCHY" << std::endl;
    auto startingJoint = mPoserAnimator.getPoserJointByName("mPelvis");
    writeBvhFragment(fileStream, avatar, startingJoint, 0);
    *fileStream << "MOTION" << std::endl;
    *fileStream << "Frames:    2" << std::endl;
    *fileStream << "Frame Time: 1" << std::endl;
    writeFirstFrameOfBvhMotion(fileStream, startingJoint);
    *fileStream << std::endl;
    writeBvhMotion(fileStream, avatar, startingJoint);
    *fileStream << std::endl;

    return true;
}

void FSFloaterPoser::writeBvhFragment(std::ostream* fileStream, LLVOAvatar* avatar, const FSPoserAnimator::FSPoserJoint* joint, S32 tabStops, bool capturedPose)
{
    if (!joint)
        return;

    auto saveAxis = getBvhJointTranslation(joint->jointName());

    switch (joint->boneType())
    {
        case WHOLEAVATAR:
            *fileStream << "ROOT " + joint->jointName() << std::endl;
            *fileStream << "{" << std::endl;
            *fileStream << getTabs(tabStops + 1) + "OFFSET " + joint->bvhOffset() << std::endl;
// <AS:chanayane> BVH fixes
            *fileStream << getTabs(tabStops + 1) + (capturedPose ? "CHANNELS 6 Xposition Yposition Zposition Xrotation Yrotation Zrotation" : "CHANNELS 6 Xposition Yposition Zposition Zrotation Xrotation Yrotation") << std::endl;
// </AS:chanayane>
            break;

        default:
            *fileStream << getTabs(tabStops) + "JOINT " + joint->jointName() << std::endl;
            *fileStream << getTabs(tabStops) + "{" << std::endl;
            *fileStream << getTabs(tabStops + 1) + "OFFSET " + joint->bvhOffset() << std::endl;

            if (capturedPose)
            {
                *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Xrotation Yrotation Zrotation" << std::endl;
                break;
            }
            switch (saveAxis)
            {
                default:
                case SWAP_NOTHING:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Xrotation Yrotation Zrotation" << std::endl;
                    break;

                case SWAP_YAW_AND_ROLL:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Zrotation Yrotation Xrotation" << std::endl;
                    break;

                case SWAP_ROLL_AND_PITCH:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Xrotation Zrotation Yrotation" << std::endl;
                    break;

                case SWAP_X2Z_Y2X_Z2Y:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Yrotation Zrotation Xrotation" << std::endl;
                    break;

                case SWAP_X2Y_Y2Z_Z2X:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Zrotation Xrotation Yrotation" << std::endl;
                    break;

                case SWAP_YAW_AND_PITCH:
                    *fileStream << getTabs(tabStops + 1) + "CHANNELS 3 Yrotation Xrotation Zrotation" << std::endl;
                    break;
            }
            break;
    }

    size_t numberOfBvhChildNodes = joint->bvhChildren().size();
    if (numberOfBvhChildNodes > 0)
    {
        for (size_t index = 0; index != numberOfBvhChildNodes; ++index)
        {
            auto nextJoint = mPoserAnimator.getPoserJointByName(joint->bvhChildren().at(index));
            writeBvhFragment(fileStream, avatar, nextJoint, tabStops + 1, capturedPose);
        }
    }
    else
    {
        // append the End Site offset
        *fileStream << getTabs(tabStops + 1) + "End Site" << std::endl;
        *fileStream << getTabs(tabStops + 1) + "{" << std::endl;
        *fileStream << getTabs(tabStops + 2) + "OFFSET " + joint->bvhEndSite() << std::endl;
        *fileStream << getTabs(tabStops + 1) + "}" << std::endl;
    }

    *fileStream << getTabs(tabStops) + "}" << std::endl;
}

void FSFloaterPoser::writeFirstFrameOfBvhMotion(std::ostream* fileStream, const FSPoserAnimator::FSPoserJoint* joint)
{
    if (!joint)
        return;

    switch (joint->boneType())
    {
        case WHOLEAVATAR:
            *fileStream << "0.000000 0.000000 0.000000 0.0 0.0 0.0";
            break;

        default:
            *fileStream << "  0.0 0.0 0.0";
            break;
    }

    size_t numberOfBvhChildNodes = joint->bvhChildren().size();
    for (size_t index = 0; index != numberOfBvhChildNodes; ++index)
    {
        auto nextJoint = mPoserAnimator.getPoserJointByName(joint->bvhChildren().at(index));
        writeFirstFrameOfBvhMotion(fileStream, nextJoint);
    }
}

void FSFloaterPoser::writeBvhMotion(llofstream* fileStream, LLVOAvatar* avatar, const FSPoserAnimator::FSPoserJoint* joint)
{
    if (!joint)
        return;

    bool lockPelvisJoint = gSavedSettings.getBool(std::string(POSER_UNLOCKPELVISINBVH_SAVE_KEY).c_str());

    auto rotation = mPoserAnimator.getJointExportRotation(avatar, *joint, !lockPelvisJoint);
    auto position = mPoserAnimator.getJointPosition(avatar, *joint);

    switch (joint->boneType())
    {
        case WHOLEAVATAR:
            *fileStream << positionToString(position) + " " + rotationToString(rotation);
            break;

        default:
            *fileStream << " " + rotationToString(rotation);
            break;
    }

    size_t numberOfBvhChildNodes = joint->bvhChildren().size();
    for (size_t index = 0; index != numberOfBvhChildNodes; ++index)
    {
        auto nextJoint = mPoserAnimator.getPoserJointByName(joint->bvhChildren().at(index));
        writeBvhMotion(fileStream, avatar, nextJoint);
    }
}

std::string FSFloaterPoser::positionToString(const LLVector3& val)
{
    constexpr F32 metersToInches = 39.37008f;
    return std::to_string(metersToInches * val[VY]) + " " + std::to_string(metersToInches * val[VZ]) + " " + std::to_string(metersToInches * val[VX]);
}

std::string FSFloaterPoser::rotationToString(const LLVector3& val)
{
    return std::to_string(val[VX] * RAD_TO_DEG) + " " + std::to_string(val[VY] * RAD_TO_DEG) + " " + std::to_string(val[VZ] * RAD_TO_DEG);
}

std::string FSFloaterPoser::getTabs(S32 numOfTabstops)
{
    std::string tabSpaces;
    for (S32 i = 0; i < numOfTabstops; i++)
        tabSpaces += "\t";

    return tabSpaces;
}

E_BoneAxisTranslation FSFloaterPoser::getBvhJointTranslation(const std::string& jointName) const
{
    if (jointName.empty())
        return SWAP_X2Y_Y2Z_Z2X;

    bool hasTransformParameter = hasString(BVH_JOINT_TRANSFORM_STRING_PREFIX + jointName);
    if (!hasTransformParameter)
        return SWAP_X2Y_Y2Z_Z2X;

    std::string paramValue = getString(BVH_JOINT_TRANSFORM_STRING_PREFIX + jointName);

    if (strstr(paramValue.c_str(), "SWAP_YAW_AND_ROLL"))
        return SWAP_YAW_AND_ROLL;
    else if (strstr(paramValue.c_str(), "SWAP_YAW_AND_PITCH"))
        return SWAP_YAW_AND_PITCH;
    else if (strstr(paramValue.c_str(), "SWAP_ROLL_AND_PITCH"))
        return SWAP_ROLL_AND_PITCH;
    else if (strstr(paramValue.c_str(), "SWAP_X2Y_Y2Z_Z2X"))
        return SWAP_X2Y_Y2Z_Z2X;
    else if (strstr(paramValue.c_str(), "SWAP_X2Z_Y2X_Z2Y"))
        return SWAP_X2Z_Y2X_Z2Y;
    else
        return SWAP_NOTHING;
}

S32 FSFloaterPoser::getBvhJointNegation(const std::string& jointName) const
{
    S32 result = NEGATE_NOTHING;

    if (jointName.empty())
        return result;

    bool hasTransformParameter = hasString(BVH_JOINT_TRANSFORM_STRING_PREFIX + jointName);
    if (!hasTransformParameter)
        return result;

    std::string paramValue = getString(BVH_JOINT_TRANSFORM_STRING_PREFIX + jointName);

    if (strstr(paramValue.c_str(), "NEGATE_YAW"))
        result |= NEGATE_YAW;
    if (strstr(paramValue.c_str(), "NEGATE_PITCH"))
        result |= NEGATE_PITCH;
    if (strstr(paramValue.c_str(), "NEGATE_ROLL"))
        result |= NEGATE_ROLL;
    if (strstr(paramValue.c_str(), "NEGATE_ALL"))
        return NEGATE_ALL;

    return result;
}


// Chaos Emerging extension: record a time sequence of genuine Firestorm joint
// transforms; the original savePoseToXml/2-frame BVH pose export stays intact.
void FSFloaterPoser::onRecordStartClicked(void* userdata)
{
    static_cast<FSFloaterPoser*>(userdata)->beginRecording();
}

void FSFloaterPoser::onRecordStopClicked(void* userdata)
{
    static_cast<FSFloaterPoser*>(userdata)->stopRecording();
}

void FSFloaterPoser::onRecordExportClicked(void* userdata)
{
    static_cast<FSFloaterPoser*>(userdata)->exportRecordedBvh();
}

void FSFloaterPoser::updateRecorderButtons()
{
    if (!mAvatarSelectionScrollList) return;
    LLVOAvatar* avatar = getUiSelectedAvatar();
    const bool active = avatar && mPoserAnimator.isPosingAvatar(avatar);
    const bool allowed = inventoryExportAllowed() && avatar &&
                         !mPosesWithUnknownSource.count(avatar->getID());
    const bool inventory = mInventoryItemId.notNull();
    if (mRecordStartButton) mRecordStartButton->setEnabled(active && allowed && !inventory && !mRecordingActive);
    if (mRecordStopButton) mRecordStopButton->setEnabled(mRecordingActive && allowed);
    if (mRecordExportButton)
        mRecordExportButton->setEnabled(allowed && !mRecordingActive && !mInventoryPlaying &&
            (inventory ? (active && avatar->isSelf() && inventoryBvhCompatible()) : mRecordedFrames.size() >= 2));
    if (mStaticExportButton) mStaticExportButton->setEnabled(active && allowed && !mRecordingActive && !mInventoryPlaying);
    if (mSavePosesBtn && !allowed) mSavePosesBtn->setEnabled(false);
    if (mAlsoSaveBvhCbx) mAlsoSaveBvhCbx->setEnabled(allowed && active);
    updateInventoryControls();
}

void FSFloaterPoser::beginRecording()
{
    if (mInventoryItemId.notNull() || !inventoryExportAllowed()) return;
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar || !mPoserAnimator.isPosingAvatar(avatar))
    {
        if (mRecordStatusText) mRecordStatusText->setText("Start posing");
        return;
    }
    if (!inventoryExportAllowed() || mPosesWithUnknownSource.count(avatar->getID()))
    {
        // Preserve Firestorm's protection against exporting imported poses
        // whose ownership has not been established.
        if (mRecordStatusText) mRecordStatusText->setText("Import locked");
        return;
    }
    mRecordingAvatarId = avatar->getID();
    mRecordedFrames.clear();
    mRecordingBegan = std::chrono::steady_clock::now();
    mRecordingActive = true;
    recordCurrentFrame();
    if (mRecordStatusText) mRecordStatusText->setText("REC 0:00");
    updateRecorderButtons();
}

void FSFloaterPoser::stopRecording()
{
    if (!mRecordingActive) return;
    mRecordingActive = false;
    LLVOAvatar* avatar = getAvatarByUuid(mRecordingAvatarId);
    if (avatar && mPoserAnimator.isPosingAvatar(avatar) &&
        !mPosesWithUnknownSource.count(avatar->getID()) && mRecordedFrames.size() < 3600)
    {
        RecorderFrame last = capturePoserFrame(avatar);
        if (!last.empty() && (mRecordedFrames.empty() || last.size() == mRecordedFrames.front().size()))
            mRecordedFrames.push_back(std::move(last));
    }
    if (mRecordStatusText)
    {
        const size_t count = mRecordedFrames.size();
        mRecordStatusText->setText(llformat("%u frames", (unsigned int)count));
    }
    updateRecorderButtons();
}

FSFloaterPoser::RecorderFrame FSFloaterPoser::capturePoserFrame(LLVOAvatar* avatar)
{
    RecorderFrame frame;
    if (!avatar || !avatar->getRootJoint() || !mPoserAnimator.isPosingAvatar(avatar)) return frame;
    const auto* root = mPoserAnimator.getPoserJointByName("mPelvis");
    if (!root) return frame;
    std::function<void(const FSPoserAnimator::FSPoserJoint*)> walk =
        [&](const FSPoserAnimator::FSPoserJoint* joint)
        {
            if (!joint) return;
            LLJoint* live = avatar->getRootJoint()->findJoint(joint->jointName());
            const bool posed = live && mPoserAnimator.isPosingAvatarJoint(avatar, *joint);
            // Capture the displayed local transform including the base rotation.
            LLQuaternion rotation;
            LLVector3 position;
            if (posed)
            {
                rotation = live->getRotation();
                if (joint->boneType() == WHOLEAVATAR) position = live->getPosition();
            }
            frame.push_back({rotation, position, joint->boneType() == WHOLEAVATAR, posed});
            for (const auto& child : joint->bvhChildren())
                walk(mPoserAnimator.getPoserJointByName(child));
        };
    walk(root);
    return frame;
}

void FSFloaterPoser::recordCurrentFrame()
{
    LLVOAvatar* avatar = getAvatarByUuid(mRecordingAvatarId);
    if (!avatar || !mPoserAnimator.isPosingAvatar(avatar) ||
        mPosesWithUnknownSource.count(avatar->getID()))
    {
        stopRecording();
        return;
    }
    RecorderFrame frame = capturePoserFrame(avatar);
    if (frame.empty() || (!mRecordedFrames.empty() && frame.size() != mRecordedFrames.front().size()))
    {
        stopRecording();
        return;
    }
    if (mRecordedFrames.size() < 3600) mRecordedFrames.push_back(std::move(frame));
}

void FSFloaterPoser::onStaticExportClicked(void* userdata)
{
    static_cast<FSFloaterPoser*>(userdata)->exportStaticBvh();
}

void FSFloaterPoser::exportStaticBvh()
{
    if (mRecordingActive || mInventoryPlaying || !inventoryExportAllowed()) return;
    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar || !mPoserAnimator.isPosingAvatar(avatar))
    {
        if (mRecordStatusText) mRecordStatusText->setText("Start posing");
        return;
    }
    if (mPosesWithUnknownSource.count(avatar->getID()) ||
        (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar)))
    {
        if (mRecordStatusText) mRecordStatusText->setText("Export denied");
        return;
    }
    const RecorderFrame frame = capturePoserFrame(avatar);
    if (frame.empty()) return;
    // Separate from the recording buffer. Two identical playable keys.
    exportCapturedBvh(avatar, {frame, frame}, true);
}

void FSFloaterPoser::exportRecordedBvh()
{
    if (!inventoryExportAllowed()) return;
    if (mInventoryItemId.notNull()) { exportInventoryAnimation(false); return; }
    if (mRecordingActive || mRecordedFrames.size() < 2) return;
    LLVOAvatar* avatar = getAvatarByUuid(mRecordingAvatarId);
    if (!avatar || mPosesWithUnknownSource.count(avatar->getID()) ||
        (!havePermissionToAnimateAvatar(avatar) && !havePermissionToAnimateOtherAvatar(avatar)))
    {
        if (mRecordStatusText) mRecordStatusText->setText("Export denied");
        return;
    }
    exportCapturedBvh(avatar, mRecordedFrames, false);
}

void FSFloaterPoser::exportCapturedBvh(LLVOAvatar* avatar,
    const std::vector<RecorderFrame>& frames, bool staticPose, F32 frameTime)
{
    if (frames.size() < 2 || frames.front().empty()) return;
    const auto* root = mPoserAnimator.getPoserJointByName("mPelvis");
    if (!root) return;
    const size_t n = frames.front().size();
    for (const auto& frame : frames) if (frame.size() != n) return;

    // Read exact per-joint translation metadata from the animation uploader.
    class RecorderBvhCheck : public LLBVHLoader
    {
    public:
        using LLBVHLoader::LLBVHLoader;
        const auto& joints() const { return mJoints; }
    };
    std::ostringstream hierarchy;
    hierarchy << "HIERARCHY\n";
    writeBvhFragment(&hierarchy, avatar, root, 0, true);
    const std::string hierarchyText = hierarchy.str();
    std::ostringstream probe;
    probe << hierarchyText << "MOTION\nFrames: 3\nFrame Time: 0.033333333\n";
    for (int t = 0; t < 3; ++t)
    {
        for (size_t j = 0; j < n; ++j)
        {
            if (j) probe << ' ';
            if (frames.front()[j].root) probe << "0 0 0 ";
            probe << "0 0 0";
        }
        probe << '\n';
    }
    ELoadStatus mapStatus = E_ST_OK;
    S32 mapLine = 0;
    strings_map_t mapAliases = avatar->getJointAliases();
    const std::string probeText = probe.str();
    RecorderBvhCheck mapping(probeText.c_str(), mapStatus, mapLine, mapAliases);
    if (!mapping.isInitialized() || mapStatus != E_ST_OK || mapping.joints().size() != n)
    {
        if (mRecordStatusText) mRecordStatusText->setText("Invalid BVH mapping");
        llwarns << "Poser BVH mapping failed at line " << mapLine << llendl;
        return;
    }
    auto toBvh = [&](size_t j, const LLQuaternion& local)
    {
        const auto* joint = mapping.joints()[j];
        const LLQuaternion basis(joint->mFrameMatrix);
        const LLQuaternion offset(joint->mOffsetMatrix);
        LLQuaternion q = basis * local * ~offset * ~basis;
        q.normalize();
        LLVector3 angles;
        q.getEulerAngles(&angles.mV[VX], &angles.mV[VY], &angles.mV[VZ]);
        return angles;
    };

    std::vector<bool> included(n, false);
    std::vector<size_t> first(n, 0);
    for (size_t j = 0; j < n; ++j)
        for (size_t t = 0; t < frames.size(); ++t)
            // Unrecognized 3-channel joints retain position serialization
            // in the uploader, introducing unwanted zero position keys.
            // Leave these nodes in the hierarchy without animating them.
            if (frames[t][j].posed &&
                (frames[t][j].root || mapping.joints()[j]->mIgnorePositions))
            {
                included[j] = true;
                first[j] = t;
                break;
            }
    std::vector<RecorderJoint> held = frames.front();
    for (size_t j = 0; j < n; ++j)
        if (included[j]) held[j] = frames[first[j]][j];

    std::ostringstream bvh;
    bvh << hierarchyText;
    bvh << "MOTION\nFrames:    " << (frames.size() + 1) << "\n";
    bvh << "Frame Time: " << std::fixed << std::setprecision(9) << (staticPose ? 1.f : frameTime) << "\n";

    // The uploader drops the reference row. Non-root channels are absolute:
    // vary the reference to retain identity/static keys. Pelvis is relative.
    for (size_t j = 0; j < n; ++j)
    {
        if (j) bvh << ' ';
        if (held[j].root) bvh << "0 0 0 ";
        LLVector3 reference;
        if (included[j] && !held[j].root)
        {
            reference = toBvh(j, held[j].rotation);
            reference.mV[VX] += F_PI_BY_TWO;
        }
        bvh << rotationToString(reference);
    }
    bvh << '\n';

    for (const RecorderFrame& frame : frames)
    {
        for (size_t j = 0; j < n; ++j)
        {
            if (j) bvh << ' ';
            if (frame[j].posed) held[j] = frame[j];
            const RecorderJoint& joint = held[j];
            if (joint.root)
                bvh << positionToString(included[j] ? joint.position : LLVector3()) << ' ';
            bvh << rotationToString(included[j] ? toBvh(j, joint.rotation) : LLVector3());
        }
        bvh << '\n';
    }

    const std::string bvhText = bvh.str();
    ELoadStatus status = E_ST_OK;
    S32 errorLine = 0;
    strings_map_t aliases = avatar->getJointAliases();
    RecorderBvhCheck check(bvhText.c_str(), status, errorLine, aliases);
    if (!check.isInitialized() || status != E_ST_OK || check.joints().size() != n)
    {
        if (mRecordStatusText) mRecordStatusText->setText("Invalid BVH");
        llwarns << "Poser capture BVH validation failed at line " << errorLine << " status " << status << llendl;
        return;
    }

    // Verify importer-space rotations against captured local quaternions.
    size_t animated = 0;
    for (size_t j = 0; j < n; ++j)
    {
        const auto* joint = check.joints()[j];
        if (!included[j]) continue;
        if (!joint || joint->mKeys.size() != frames.size() + 1)
        {
            if (mRecordStatusText) mRecordStatusText->setText("Invalid keys");
            return;
        }
        if (joint->mIgnore && !frames.front()[j].root)
        {
            if (mRecordStatusText) mRecordStatusText->setText("Missing joint");
            llwarns << "Poser export lost controlled joint " << joint->mName << llendl;
            return;
        }
        if (!joint->mIgnore) ++animated;
        const LLQuaternion f(joint->mFrameMatrix);
        const LLQuaternion offset(joint->mOffsetMatrix);
        LLQuaternion reference;
        if (joint->mRelativeRotationKey)
        {
            const auto& r = joint->mKeys[0].mRot;
            reference = mayaQ(r[0],r[1],r[2],LLQuaternion::ZYX);
        }
        RecorderJoint expected = frames[first[j]][j];
        for (size_t t = 0; t < frames.size(); ++t)
        {
            if (frames[t][j].posed) expected = frames[t][j];
            const auto& r = joint->mKeys[t+1].mRot;
            const LLQuaternion q = ~f * mayaQ(r[0],r[1],r[2],LLQuaternion::ZYX) * ~reference * f * offset;
            if (expected.root)
            {
                const auto& raw = joint->mKeys[t+1].mPos;
                LLVector3 imported(raw);
                imported = imported * f * offset;
                imported *= 0.02540005f;
                imported -= joint->mRelativePosition;
                if (dist_vec(imported, expected.position) > 0.0002f)
                {
                    if (mRecordStatusText) mRecordStatusText->setText("Root mismatch");
                    llwarns << "Poser BVH pelvis position roundtrip failed at frame " << t << llendl;
                    return;
                }
            }
            F32 dot = 0.f;
            for (int k = 0; k < 4; ++k) dot += q.mQ[k] * expected.rotation.mQ[k];
            if (!std::isfinite(dot) || std::fabs(dot) < 0.99998f)
            {
                if (mRecordStatusText) mRecordStatusText->setText("Pose mismatch");
                llwarns << "Poser BVH rotation roundtrip failed: " << joint->mName
                        << " frame " << t << " dot " << dot << " expected " << expected.rotation << " imported " << q << llendl;
                return;
            }
        }
    }
    if (!animated)
    {
        if (mRecordStatusText) mRecordStatusText->setText("No joints");
        return;
    }
    llinfos << "Poser BVH rotation roundtrip validated " << animated
            << " animated joints, " << frames.size() << " playable frames" << llendl;
    std::time_t timestamp = std::time(nullptr);
    std::tm localTime = {};
#if LL_WINDOWS
    localtime_s(&localTime, &timestamp);
#else
    localtime_r(&timestamp, &localTime);
#endif
    char timeLabel[32] = {};
    std::strftime(timeLabel, sizeof(timeLabel), "%Y%m%d_%H%M%S", &localTime);
    const std::string filename = std::string(staticPose ? "poser_static_" : "poser_recording_") + timeLabel + ".bvh";
#if LL_WINDOWS
    // Reuse the viewer's native Explorer Save As dialog. On Windows
    // HBFileSelector::saveFile invokes its callback synchronously, including
    // with an empty path on cancellation. No recording is consumed on cancel.
    std::string fullPath;
    if (HBFileSelector::isInUse())
        return;
    HBFileSelector::saveFile(HBFileSelector::FFSAVE_BVH, filename,
        [](HBFileSelector::ESaveFilter, std::string& selectedPath, void* data)
        {
            *static_cast<std::string*>(data) = selectedPath;
        }, &fullPath);
    if (fullPath.empty())
        return;
#else
    const std::string folder = FSPoserFilePaths::getExpandedFilename(LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY);
    if (!FSPoserFilePaths::fileExists(folder))
        LLFile::mkdir(folder);
    const std::string fullPath = FSPoserFilePaths::getExpandedFilename(
        LL_PATH_USER_SETTINGS, POSE_SAVE_SUBDIRECTORY, filename);
#endif
    llofstream file(fullPath.c_str());
    if (!file.is_open())
    {
        if (mRecordStatusText) mRecordStatusText->setText("Save failed");
        return;
    }
    file << bvhText;
    file.flush();
    const bool good = file.good();
    file.close();
    if (mRecordStatusText) mRecordStatusText->setText(good ? "BVH saved" : "Save failed");
    if (good)
        llinfos << (staticPose ? "Saved static Poser BVH: " : "Saved 30 FPS multi-frame Poser recording: ") << fullPath << llendl;
}

bool FSFloaterPoser::getSavingToBvh() const
{
    return gSavedSettings.getBool(std::string(POSER_SAVEEXTERNALFORMAT_SAVE_KEY).c_str());
}

void FSFloaterPoser::onClickSavingToBvh()
{
    mUnlockPelvisInBvhSaveCbx->setVisible(getSavingToBvh());
    refreshTextHighlightingOnJointScrollLists();
}

void FSFloaterPoser::onClickLockWorldRotBtn()
{
    auto selectedJoints = getUiSelectedPoserJoints();
    if (selectedJoints.empty())
        return;

    LLVOAvatar* avatar = getUiSelectedAvatar();
    if (!avatar)
        return;

    if (!mPoserAnimator.isPosingAvatar(avatar))
        return;

    for (auto item : selectedJoints)
    {
        bool currentlyPosingJoint = mPoserAnimator.isPosingAvatarJoint(avatar, *item);
        if (!currentlyPosingJoint)
            continue;

        bool newLockState = !mPoserAnimator.getRotationIsWorldLocked(avatar, *item);
        mPoserAnimator.setRotationIsWorldLocked(avatar, *item, newLockState);
    }

    refreshTextHighlightingOnJointScrollLists();
}

FSLoadPoseTimer::FSLoadPoseTimer(FSLoadPoseTimer::callback_t callback) : LLEventTimer(0.5f), mCallback(callback)
{
}

bool FSLoadPoseTimer::tick()
{
    if (!mAttemptLoading)
        return false;
    if (!mCallback)
        return false;

    if (mLoadAttempts >= mMaxLoadAttempts)
        mAttemptLoading = false;

    mLoadAttempts++;
    mCallback();
    return false;
}

void FSLoadPoseTimer::tryLoading(std::string filePath, E_LoadPoseMethods loadMethod)
{
    mPoseFullPath   = filePath;
    mLoadType       = loadMethod;
    mLoadAttempts   = 0;
    mAttemptLoading = true;
}


// Cool VL / Firestorm 7.2.5 event-system bridge.
void FSFloaterPoser::dispatchLegacyAction(LLUICtrl* control,
                                          const std::string& action,
                                          const LLSD& argument)
{
    auto it = mCommitCallbackRegistrar.actions.find(action);
    if (it != mCommitCallbackRegistrar.actions.end())
        it->second(control, argument);
}

void FSFloaterPoser::onLegacyCommit(LLUICtrl* control, void* userdata)
{
    auto* binding = static_cast<ChaosLegacyBinding*>(userdata);
    if (binding && binding->owner)
        binding->owner->dispatchLegacyAction(control, binding->action, binding->parameter);
}

void FSFloaterPoser::onLegacyButton(void* userdata)
{
    auto* binding = static_cast<ChaosLegacyBinding*>(userdata);
    if (!binding || !binding->owner)
        return;
    LLUICtrl* control = binding->owner->getChild<LLUICtrl>(binding->name.c_str());
    binding->owner->dispatchLegacyAction(control, binding->action, binding->parameter);
}

void FSFloaterPoser::bindLegacyActions()
{
    for (const FSPoserLegacyBindingSpec& spec : FS_POSER_LEGACY_BINDINGS)
    {
        LLUICtrl* control = getChild<LLUICtrl>(spec.control);
        if (!control)
            continue;
        auto binding = std::make_unique<ChaosLegacyBinding>();
        binding->owner = this;
        binding->name = spec.control;
        binding->action = spec.action;
        binding->parameter = spec.parameter[0] ? LLSD(spec.parameter) : LLSD();
        if (auto* button = dynamic_cast<LLButton*>(control))
            button->setClickedCallback(&FSFloaterPoser::onLegacyButton, binding.get());
        else
        {
            control->setCommitCallback(&FSFloaterPoser::onLegacyCommit);
            control->setCallbackUserData(binding.get());
        }
        mLegacyBindings.push_back(std::move(binding));
    }
}

void FSFloaterPoser::onLegacyLockWorldRotation(void* userdata)
{
    auto* self = static_cast<FSFloaterPoser*>(userdata);
    if (self)
        self->onClickLockWorldRotBtn();
}

void FSFloaterPoser::onLegacyDirectCommit(LLUICtrl* control, void* userdata)
{
    auto* self = static_cast<FSFloaterPoser*>(userdata);
    if (!self || !control)
        return;
    if (control == self->mAvatarTrackball)
        self->onTrackballChanged();
    else if (control == self->mJointsTabs)
    {
        self->onJointTabSelect();
        self->setRotationChangeButtons(false, false);
    }
    else if (control == self->mAvatarSelectionScrollList)
        self->onAvatarSelect();
    else if (control == self->mBodyJointsScrollList ||
             control == self->mFaceJointsScrollList ||
             control == self->mHandJointsScrollList ||
             control == self->mMiscJointsScrollList ||
             control == self->mCollisionVolumesScrollList)
        self->onJointTabSelect();
    else if (control == self->mPosesScrollList)
        self->onPoseFileSelect();
    else if (control == self->mAlsoSaveBvhCbx)
        self->onClickSavingToBvh();
    else if (control == self->mYawSpnr || control == self->mPitchSpnr ||
             control == self->mRollSpnr)
        self->onYawPitchRollChanged();
}

// Native Cool VL popup for the *original* Firestorm menu_fs_poser_poses_btn.xml.
void FSFloaterPoser::buildPoseLoadMenu()
{
    if (mPoseLoadMenu || !gMenuHolderp)
        return;
    mPoseLoadMenu = new LLMenuGL("fs_poser_poses_btn", "Load Pose");
    gMenuHolderp->addChild(mPoseLoadMenu);
    mPoseLoadMenu->setVisible(false);
    for (const FSPoserLegacyMenuEntry& e : FS_POSER_LEGACY_MENU_ENTRIES)
    {
        if (e.separator)
        {
            mPoseLoadMenu->appendSeparator();
            continue;
        }
        auto action = std::make_unique<ChaosPoseMenuItem>();
        action->owner = this;
        action->parameter = e.parameter;
        LLMenuItemCallGL* item = new LLMenuItemCallGL(e.name, e.label,
                &FSFloaterPoser::onPoseMenuItem, nullptr, action.get(),
                KEY_NONE, MASK_NONE, true);
        mPoseLoadActions.push_back(std::move(action));
        mPoseLoadMenu->append(item);
    }
}

void FSFloaterPoser::onShowPoseMenu(void* userdata)
{
    auto* self = static_cast<FSFloaterPoser*>(userdata);
    if (!self || !self->mLoadPosesBtn || !self->mLoadPosesBtn->getEnabled())
        return;
    if (!self->mPoseLoadMenu)
        self->buildPoseLoadMenu();
    if (self->mPoseLoadMenu)
        LLMenuGL::showPopup(self->mLoadPosesBtn, self->mPoseLoadMenu, 0, 0);
}

void FSFloaterPoser::onPoseMenuItem(void* userdata)
{
    auto* item = static_cast<ChaosPoseMenuItem*>(userdata);
    if (item && item->owner)
        item->owner->dispatchLegacyAction(nullptr, "Pose.Menu", LLSD(item->parameter));
}

bool FSFloaterPoser::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
    EDragAndDropType cargo_type, void* cargo_data, EAcceptance* accept, std::string& tooltip_msg)
{
    if (cargo_type != DAD_ANIMATION)
        return LLFloater::handleDragAndDrop(x,y,mask,drop,cargo_type,cargo_data,accept,tooltip_msg);
    auto* item = static_cast<LLInventoryItem*>(cargo_data);
    const auto* in = item ? gInventory.getItem(item->getUUID()) : nullptr;
    *accept = in && gInventory.isObjectDescendentOf(in->getUUID(),gInventory.getRootFolderID())
              && !mRecordingActive ? ACCEPT_YES_COPY_SINGLE : ACCEPT_NO;
    if (drop && *accept == ACCEPT_YES_COPY_SINGLE) loadInventoryAnimation(in->getUUID());
    return true;
}

void FSFloaterPoser::onInventoryLoad(void* data)
{
    auto* self = static_cast<FSFloaterPoser*>(data);
    LLFloaterInventory* inv = LLFloaterInventory::getActiveFloater();
    uuid_vec_t selected;
    if (inv && inv->getActivePanel() && inv->getActivePanel()->getRootFolder())
        inv->getActivePanel()->getRootFolder()->getSelection(selected);
    if (selected.size() == 1)
    {
        const auto* item = gInventory.getItem(selected.front());
        if (item && item->getType() == LLAssetType::AT_ANIMATION)
        {
            self->loadInventoryAnimation(selected.front());
            return;
        }
    }
    LLFloaterInventory::showAgentInventory();
    self->mInventoryStatus->setText("Select an animation and click Load, or drag it here.");
}
void FSFloaterPoser::loadInventoryAnimation(const LLUUID& id)
{
    if (mRecordingActive || !gAgentAvatarp) return;
    LLViewerInventoryItem* item = gInventory.getItem(id);
    if (!item || !gInventory.isObjectDescendentOf(id,gInventory.getRootFolderID()))
        return;
    if (item->getIsLinkType()) item = item->getLinkedItem();
    if (!item || !item->isFinished() || item->getActualType()!=LLAssetType::AT_ANIMATION ||
        item->getAssetUUID().isNull())
    {
        mInventoryStatus->setText("Animation inventory data unavailable.");
        return;
    }
    clearInventoryAnimation(false);
    mInventoryItemId = item->getUUID();
    mInventoryAssetId = item->getAssetUUID();
    mInventoryAgentId = gAgentID;
    mPosesWithUnknownSource.insert(mInventoryAgentId);
    mRecordedFrames.clear();
    mRecordingAvatarId.setNull();
    setUiSelectedAvatar(gAgentAvatarp->getID());
    if (!mPoserAnimator.isPosingAvatar(gAgentAvatarp))
        mPoserAnimator.tryPosingAvatar(gAgentAvatarp);
    mInventoryLoadStart = std::chrono::steady_clock::now();
    mInventoryStatus->setText("Loading: " + item->getName());
    // Use the same motion fetch/decode/cache as normal viewer animation playback.
    // Creating the motion does not start it in-world.
    gAgentAvatarp->createMotion(mInventoryAssetId);
    updateRecorderButtons();
}

bool FSFloaterPoser::inventoryExportAllowed() const
{
    if (mInventoryItemId.isNull()) return true;
    const auto* item = gInventory.getItem(mInventoryItemId);
    return mInventoryMotion && mInventoryAgentId == gAgentID &&
           item && item->isFinished() && !item->getIsLinkType() &&
           item->getActualType() == LLAssetType::AT_ANIMATION &&
           item->getAssetUUID() == mInventoryAssetId &&
           item->getCreatorUUID() == gAgentID;
}

void FSFloaterPoser::clearInventoryAnimation(bool restart)
{
    const bool had = mInventoryItemId.notNull();
    mInventoryPlaying = false;
    mInventoryNeedsApply = false;
    mInventoryMotion.reset();
    mInventoryShown.clear();
    mInventoryItemId.setNull(); mInventoryAssetId.setNull();
    mInventoryAgentId.setNull();
    mInventoryTime = 0.f;
    if (had && gAgentAvatarp)
    {
        mPoserAnimator.setInventoryTimelinePreview(gAgentAvatarp,false);
        if (restart)
        {
            // End the imported pose, and start an explicitly clean pose.
            if (mPoserAnimator.isPosingAvatar(gAgentAvatarp))
                mPoserAnimator.stopPosingAvatar(gAgentAvatarp);
            mPoserAnimator.tryPosingAvatar(gAgentAvatarp);
            mPoserAnimator.setAllAvatarStartingRotationsToZero(gAgentAvatarp);
            mPosesWithUnknownSource.erase(gAgentAvatarp->getID());
            mRecordedFrames.clear();
            mRecordingAvatarId.setNull();
        }
    }
    if (mInventoryTimeline) mInventoryTimeline->setValue(0.f,true);
    if (mInventoryStatus) mInventoryStatus->setText("Drag an inventory animation here, or select one and click Load.");
    if (mInventoryTimeText) mInventoryTimeText->setText("0.00 / 0.00 s");
    if (restart) { onAvatarSelect(); updateRecorderButtons(); }
}

void FSFloaterPoser::tickInventoryEditor()
{
    if (mInventoryItemId.isNull()) return;
    if (!gAgentAvatarp || mInventoryAgentId != gAgentID)
    {
        clearInventoryAnimation(false);
        return;
    }
    if (!mInventoryMotion)
    {
        if (auto* loaded = LLKeyframeDataCache::getKeyframeData(mInventoryAssetId))
        {
            if (!std::isfinite(loaded->mDuration) || loaded->mDuration <= 0.f ||
                loaded->mDuration > 120.f || loaded->mJointMotionArray.empty())
            {
                mInventoryStatus->setText("Invalid or unsupported animation duration.");
                return;
            }
            mInventoryMotion = std::make_unique<InventoryMotion>(gAgentAvatarp,*loaded);
            mInventoryTimeline->setMaxValue(loaded->mDuration);
            const bool own = inventoryExportAllowed();
            if (own) mPosesWithUnknownSource.erase(mInventoryAgentId);
            else mPosesWithUnknownSource.insert(mInventoryAgentId);
            const auto* item = gInventory.getItem(mInventoryItemId);
            mInventoryStatus->setText(std::string(own ? "Creator verified: " : "Preview only - other creator: ") +
                                     (item ? item->getName() : "Animation"));
            mInventoryNeedsApply = true;
            mInventoryTime = 0.f;
        }
        else if (std::chrono::steady_clock::now()-mInventoryLoadStart > std::chrono::seconds(30))
        {
            mInventoryStatus->setText("Asset not available. Clear and retry loading.");
            return;
        }
        else return;
    }
    if (!inventoryExportAllowed()) mPosesWithUnknownSource.insert(mInventoryAgentId);
    auto* avatar = getUiSelectedAvatar();
    if (!avatar || !avatar->isSelf() || !mPoserAnimator.isPosingAvatar(avatar))
    {
        mInventoryPlaying = false;
        mInventoryNeedsApply = true;
        return;
    }
    if (mInventoryNeedsApply)
    {
        // Release unanimated joints, then apply the imported tracks.
        // Do not bake unrelated AO/sitting poses into the original animation.
        mInventoryShown.clear();
        const auto* root = mPoserAnimator.getPoserJointByName("mPelvis");
        std::function<void(const FSPoserAnimator::FSPoserJoint*)> walk =
            [&](const FSPoserAnimator::FSPoserJoint* j)
            {
                if (!j) return;
                mPoserAnimator.setPosingAvatarJoint(avatar,*j,false);
                InventoryTransform t;
                if (mPoserAnimator.getInventoryTarget(avatar,j->jointName(),t.rotation,t.position,t.scale))
                    mInventoryShown[j->jointName()] = t;
                for (const auto& child : j->bvhChildren())
                    walk(mPoserAnimator.getPoserJointByName(child));
            };
        walk(root);
        mInventoryNeedsApply = false;
        mPoserAnimator.setInventoryTimelinePreview(avatar,true);
        applyInventoryTime(mInventoryTime,true);
        onAvatarSelect();
    }
    if (mInventoryPlaying)
    {
        F32 elapsed = std::chrono::duration<F32>(std::chrono::steady_clock::now()-mInventoryPlayStart).count();
        F32 time = mInventoryPlayOffset + elapsed;
        if (time >= mInventoryMotion->data().mDuration)
        {
            time = mInventoryMotion->data().mDuration;
            mInventoryPlaying = false;
        }
        applyInventoryTime(time,!mInventoryPlaying);
    }
}

void FSFloaterPoser::applyInventoryTime(F32 time, bool update_controls)
{
    auto* avatar = getUiSelectedAvatar();
    if (!mInventoryMotion || !avatar || !avatar->isSelf() ||
        !mPoserAnimator.isPosingAvatar(avatar)) return;
    auto& clip = mInventoryMotion->data();
    mInventoryTime = llclamp(time,0.f,clip.mDuration);
    for (auto* track : clip.mJointMotionArray)
    {
        const auto* j = mPoserAnimator.getPoserJointByName(track->mJointName);
        if (!j) continue;
        InventoryTransform t;
        if (!mPoserAnimator.getInventoryTarget(avatar,track->mJointName,t.rotation,t.position,t.scale))
            continue;
        mPoserAnimator.setPosingAvatarJoint(avatar,*j,true);
        if (!track->mRotationCurve.mKeys.empty())
            t.rotation = track->mRotationCurve.getValue(mInventoryTime,clip.mDuration);
        if (!track->mPositionCurve.mKeys.empty())
            t.position = track->mPositionCurve.getValue(mInventoryTime,clip.mDuration);
        if (!track->mScaleCurve.mKeys.empty())
            t.scale = track->mScaleCurve.getValue(mInventoryTime,clip.mDuration);
        mPoserAnimator.setInventoryTarget(avatar,track->mJointName,t.rotation,t.position,t.scale);
        mInventoryShown[track->mJointName] = t;
    }
    mInventoryTimeline->setValue(mInventoryTime,true);
    mInventoryTimeText->setText(llformat("%.2f / %.2f s",mInventoryTime,clip.mDuration));
    if (update_controls)
    {
        refreshTextHighlightingOnJointScrollLists();
        updateManipWithFirstSelectedJoint(getUiSelectedPoserJoints());
    }
}

void FSFloaterPoser::editInventoryKeys(bool whole_clip)
{
    auto* avatar = getUiSelectedAvatar();
    if (!mInventoryMotion || mInventoryPlaying || !avatar || !avatar->isSelf() ||
        !mPoserAnimator.isPosingAvatar(avatar)) return;
    auto& clip = mInventoryMotion->data();
    size_t edited = 0;
    for (const auto* joint : getUiSelectedPoserJoints())
    {
        if (!joint || !mPoserAnimator.isPosingAvatarJoint(avatar,*joint)) continue;
        const std::string name = joint->jointName();
        auto baseline = mInventoryShown.find(name);
        if (baseline == mInventoryShown.end()) continue;
        InventoryTransform target;
        if (!mPoserAnimator.getInventoryTarget(avatar,name,target.rotation,target.position,target.scale))
            continue;
        const InventoryTransform prior = baseline->second;
        auto* track = mInventoryMotion->ensureTrack(name);
        auto& rot = track->mRotationCurve;
        auto& pos = track->mPositionCurve;
        auto& scale = track->mScaleCurve;
        if (rot.mKeys.empty())
        {
            rot.mKeys.emplace(0.f,LLKeyframeMotion::RotationKey(0.f,prior.rotation));
            rot.mKeys.emplace(clip.mDuration,LLKeyframeMotion::RotationKey(clip.mDuration,prior.rotation));
        }
        if (whole_clip)
        {
            const LLQuaternion delta = target.rotation * ~prior.rotation;
            for (auto& k : rot.mKeys)
            {
                k.second.mRotation = delta * k.second.mRotation;
                k.second.mRotation.normalize();
            }
        }
        else rot.mKeys[mInventoryTime] = LLKeyframeMotion::RotationKey(mInventoryTime,target.rotation);
        rot.mNumKeys = (S32)rot.mKeys.size();
        track->mUsage |= LLJointState::ROT;
        // Preserve existing position tracks, or create one only when a joint
        // has actually been moved. Never insert zero position on untouched bones.
        if (!pos.mKeys.empty() || dist_vec(target.position,prior.position)>0.00001f)
        {
            if (pos.mKeys.empty())
            {
                pos.mKeys.emplace(0.f,LLKeyframeMotion::PositionKey(0.f,prior.position));
                pos.mKeys.emplace(clip.mDuration,LLKeyframeMotion::PositionKey(clip.mDuration,prior.position));
            }
            if (whole_clip)
                for (auto& k : pos.mKeys) k.second.mPosition += target.position-prior.position;
            else pos.mKeys[mInventoryTime] = LLKeyframeMotion::PositionKey(mInventoryTime,target.position);
            pos.mNumKeys = (S32)pos.mKeys.size();
            track->mUsage |= LLJointState::POS;
        }
        if (dist_vec(target.scale,prior.scale)>0.00001f || !scale.mKeys.empty())
        {
            if (scale.mKeys.empty())
            {
                scale.mKeys.emplace(0.f,LLKeyframeMotion::ScaleKey(0.f,prior.scale));
                scale.mKeys.emplace(clip.mDuration,LLKeyframeMotion::ScaleKey(clip.mDuration,prior.scale));
            }
            if (whole_clip)
                for (auto& k : scale.mKeys) k.second.mScale += target.scale-prior.scale;
            else scale.mKeys[mInventoryTime] = LLKeyframeMotion::ScaleKey(mInventoryTime,target.scale);
            scale.mNumKeys = (S32)scale.mKeys.size();
        }
        ++edited;
    }
    if (edited) applyInventoryTime(mInventoryTime,true);
    mInventoryStatus->setText(!inventoryExportAllowed() ? "Preview only - other creator. Exports disabled." :
        (edited ? (whole_clip ? "Clip adjustment saved to selected tracks." : "Key saved to selected tracks.") :
         "Select and enable the joints to edit."));
}

void FSFloaterPoser::stepInventoryKey(bool next)
{
    if (!mInventoryMotion) return;
    mInventoryPlaying = false;
    auto& clip = mInventoryMotion->data();
    F32 nearest = next ? clip.mDuration : 0.f;
    auto check = [&](const auto& keys)
    {
        for (const auto& entry : keys)
        {
            F32 t = entry.first;
            if (next && t > mInventoryTime+0.0001f && t<nearest) nearest=t;
            if (!next && t < mInventoryTime-0.0001f && t>nearest) nearest=t;
        }
    };
    for (auto* j : clip.mJointMotionArray)
    {
        check(j->mRotationCurve.mKeys); check(j->mPositionCurve.mKeys); check(j->mScaleCurve.mKeys);
    }
    applyInventoryTime(nearest,true);
}

void FSFloaterPoser::extendInventoryAnimation()
{
    if (!mInventoryMotion || mInventoryPlaying) return;
    auto& clip = mInventoryMotion->data();
    F32 old = clip.mDuration;
    F32 maximum = llclamp(gSavedSettings.getF32("AnimationsMaxDuration"),30.f,120.f);
    if (old>=maximum) { mInventoryStatus->setText("Animation duration limit reached."); return; }
    F32 end = llmin(old+1.f,maximum);
    for (auto* j : clip.mJointMotionArray)
    {
        auto& r=j->mRotationCurve; auto& p=j->mPositionCurve; auto& s=j->mScaleCurve;
        if (!r.mKeys.empty()) { auto v=r.getValue(old,old); r.mKeys[old]={old,v}; r.mKeys[end]={end,v}; r.mNumKeys=(S32)r.mKeys.size(); }
        if (!p.mKeys.empty()) { auto v=p.getValue(old,old); p.mKeys[old]={old,v}; p.mKeys[end]={end,v}; p.mNumKeys=(S32)p.mKeys.size(); }
        if (!s.mKeys.empty()) { auto v=s.getValue(old,old); s.mKeys[old]={old,v}; s.mKeys[end]={end,v}; s.mNumKeys=(S32)s.mKeys.size(); }
    }
    if (std::fabs(clip.mLoopOutPoint-old)<0.0001f) clip.mLoopOutPoint=end;
    clip.mDuration=end;
    mInventoryTimeline->setMaxValue(end);
    applyInventoryTime(end,true);
    mInventoryStatus->setText("Extended: adjust the new end and click Set Key.");
}

bool FSFloaterPoser::inventoryBvhCompatible() const
{
    if (!mInventoryMotion) return false;
    const auto& clip=mInventoryMotion->data();
    if (!clip.mConstraints.empty()) return false;
    for (const auto* track:clip.mJointMotionArray)
    {
        const auto* j=mPoserAnimator.getPoserJointByName(track->mJointName);
        if (!j || j->boneType()==COL_VOLUMES || !track->mScaleCurve.mKeys.empty() ||
            (track->mJointName!="mPelvis" && !track->mPositionCurve.mKeys.empty())) return false;
    }
    return true;
}

void FSFloaterPoser::exportInventoryAnimation(bool native)
{
    auto* avatar=getUiSelectedAvatar();
    if (!inventoryExportAllowed() || !mInventoryMotion || mInventoryPlaying || !avatar ||
        !avatar->isSelf() || !mPoserAnimator.isPosingAvatar(avatar) ||
        mPosesWithUnknownSource.count(avatar->getID())) return;
    auto& clip=mInventoryMotion->data();
    if (native)
    {
        if (!mInventoryMotion->canSerialize(avatar))
        { mInventoryStatus->setText("Native export cannot preserve these scale/constraint channels."); return; }
        // Serialize a separate copy: the native serializer quantizes its input.
        InventoryMotion copy(avatar,clip);
        U32 size=copy.getFileSize();
        if (!size || size>64*1024*1024) { mInventoryStatus->setText("Invalid animation size."); return; }
        std::vector<U8> bytes(size);
        LLDataPackerBinaryBuffer dp(bytes.data(),size);
        if (!copy.serialize(dp)) { mInventoryStatus->setText("Animation serialization failed."); return; }
        if (HBFileSelector::isInUse()) return;
        std::string path;
        HBFileSelector::saveFile(HBFileSelector::FFSAVE_ALL,"poser_edited.anim",
            [](HBFileSelector::ESaveFilter,std::string& chosen,void* data)
            { *static_cast<std::string*>(data)=chosen; }, &path);
        if (path.empty() || !inventoryExportAllowed()) return;
        if (path.size()<5 || path.substr(path.size()-5)!=".anim") path += ".anim";
        llofstream file(path.c_str(),std::ios::binary);
        file.write(reinterpret_cast<const char*>(bytes.data()),dp.getCurrentSize());
        file.flush();
        mInventoryStatus->setText(file.good() ? "Edited ANIM saved. Upload as a new animation." : "Save failed.");
        return;
    }
    if (!inventoryBvhCompatible()) { mInventoryStatus->setText("Use Export ANIM to preserve all channels."); return; }
    size_t intervals=std::max<size_t>(1,(size_t)std::ceil(clip.mDuration*30.f));
    std::vector<RecorderFrame> frames;
    frames.reserve(intervals+1);
    const auto* root=mPoserAnimator.getPoserJointByName("mPelvis");
    for (size_t i=0;i<=intervals;++i)
    {
        F32 time=clip.mDuration*(F32)i/(F32)intervals;
        RecorderFrame frame;
        std::function<void(const FSPoserAnimator::FSPoserJoint*)> walk =
            [&](const FSPoserAnimator::FSPoserJoint* j)
            {
                if (!j) return;
                RecorderJoint out{};
                out.root=j->boneType()==WHOLEAVATAR;
                if (auto* tr=mInventoryMotion->track(j->jointName()))
                {
                    out.posed=!tr->mRotationCurve.mKeys.empty() || (out.root && !tr->mPositionCurve.mKeys.empty());
                    if (!tr->mRotationCurve.mKeys.empty()) out.rotation=tr->mRotationCurve.getValue(time,clip.mDuration);
                    if (out.root && !tr->mPositionCurve.mKeys.empty()) out.position=tr->mPositionCurve.getValue(time,clip.mDuration);
                }
                frame.push_back(out);
                for (const auto& child:j->bvhChildren()) walk(mPoserAnimator.getPoserJointByName(child));
            };
        walk(root);
        frames.push_back(std::move(frame));
    }
    exportCapturedBvh(avatar,frames,false,clip.mDuration/(F32)intervals);
}

void FSFloaterPoser::updateInventoryControls()
{
    if (!mInventoryLoad) return;
    auto* avatar=getUiSelectedAvatar();
    bool ready=mInventoryMotion && avatar && avatar->isSelf() && mPoserAnimator.isPosingAvatar(avatar);
    bool edit=ready && !mInventoryPlaying;
    bool allowed=edit && inventoryExportAllowed() && !mPosesWithUnknownSource.count(avatar->getID());
    mInventoryLoad->setEnabled(!mRecordingActive);
    mInventoryClear->setEnabled(mInventoryItemId.notNull());
    mInventoryPlay->setEnabled(ready);
    mInventoryPlay->setLabel(mInventoryPlaying ? "Pause" : "Play");
    mInventoryTimeline->setEnabled(ready);
    for (auto* b : {mInventorySetKey,mInventoryOffset,mInventoryExtend,mInventoryPrev,mInventoryNext})
        b->setEnabled(edit);
    mInventoryExport->setEnabled(allowed && mInventoryMotion->canSerialize(avatar));
}
void FSFloaterPoser::onInventoryPlay(void* d)
{
    auto* s=static_cast<FSFloaterPoser*>(d);
    if (!s->mInventoryMotion) return;
    if (s->mInventoryPlaying) { s->mInventoryPlaying=false; s->applyInventoryTime(s->mInventoryTime,true); }
    else
    {
        if (s->mInventoryTime>=s->mInventoryMotion->data().mDuration) s->mInventoryTime=0.f;
        s->mInventoryPlayOffset=s->mInventoryTime;
        s->mInventoryPlayStart=std::chrono::steady_clock::now();
        s->mInventoryPlaying=true;
    }
    s->updateRecorderButtons();
}
void FSFloaterPoser::onInventoryClear(void* d) { static_cast<FSFloaterPoser*>(d)->clearInventoryAnimation(true); }
void FSFloaterPoser::onInventorySetKey(void* d) { static_cast<FSFloaterPoser*>(d)->editInventoryKeys(false); }
void FSFloaterPoser::onInventoryOffset(void* d) { static_cast<FSFloaterPoser*>(d)->editInventoryKeys(true); }
void FSFloaterPoser::onInventoryExtend(void* d) { static_cast<FSFloaterPoser*>(d)->extendInventoryAnimation(); }
void FSFloaterPoser::onInventoryPrev(void* d) { static_cast<FSFloaterPoser*>(d)->stepInventoryKey(false); }
void FSFloaterPoser::onInventoryNext(void* d) { static_cast<FSFloaterPoser*>(d)->stepInventoryKey(true); }
void FSFloaterPoser::onInventoryExport(void* d) { static_cast<FSFloaterPoser*>(d)->exportInventoryAnimation(true); }
void FSFloaterPoser::onInventorySeek(LLUICtrl* c,void* d)
{
    auto* s=static_cast<FSFloaterPoser*>(d);
    s->mInventoryPlaying=false;
    s->applyInventoryTime((F32)c->getValue().asReal(),true);
    s->updateRecorderButtons();
}