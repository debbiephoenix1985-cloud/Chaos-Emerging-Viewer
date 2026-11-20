/**
 * @file ao.cpp
 * @brief Anything concerning the Viewer Side Animation Overrider GUI
 *
 * $LicenseInfo:firstyear=2001&license=viewerlgpl$
 * Second Life Viewer Source Code
 * Copyright (C) 2010, Linden Research, Inc.
 * Copyright (C) 2011, Zi Ree @ Second Life
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

#include "llviewerprecompiledheaders.h"

#include "ao.h"
#include "aoengine.h"
#include "aoset.h"
#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llinventorymodel.h"

#include "llmenugl.h"

#include "llscrolllistctrl.h"
#include "llspinctrl.h"

#include "lluictrlfactory.h"
#include "llviewercontrol.h"

#include "llviewerinventory.h"
#include "llnotify.h"
#include "llfloaterinventory.h"
#include "llfolderview.h"
#include "llviewermenu.h"


#include <functional>

// Same ASCII-and-pipe validation as the original Firestorm AO name rules,
// expressed through the native Chaos text validation API.
static bool validAOName(const LLWString& value)
{
    return std::all_of(value.begin(), value.end(),
        [](wchar_t c) { return c >= 0x20 && c <= 0x7e && c != L'|'; });
}
FloaterAO::FloaterAO(const LLSD& key)
:   LLFloater(), LLEventTimer(10.f),
    mSetList(0),
    mSelectedSet(0),
    mSelectedState(0),
    mCloneSetItem(nullptr),
    mCanDragAndDrop(false),
    mImportRunning(false),
    mCurrentBoldItem(nullptr),
    mMore(true),
    mRenameButton(nullptr),
    mTrackSelector(nullptr),
    mTrackAddButton(nullptr),
    mDropZonePanel(nullptr),
    mDropZoneHovered(false),
    mRebuildPending(false),
    mCurrentTrack(0)
{
    mEventTimer.stop();
    // Chaos legacy floater builds XUI at construction rather than registration.
    LLUICtrlFactory::getInstance()->buildFloater(this, "floater_ao.xml");
}

FloaterAO::~FloaterAO()
{
    if (mContextMenu) mContextMenu->die();
    if (mAddMenu) mAddMenu->die();
}

// virtual
void FloaterAO::draw()
{
    if (mDropZonePanel && mDropZonePanel->getVisible())
    {
        if (mDropZoneHovered)
        {
            mDropZoneHovered = false;
        }
        else
        {
            mDropZonePanel->setVisible(false);
        }
    }

    LLFloater::draw();
}

void FloaterAO::reloading(bool reload)
{
    if (reload)
    {
        mEventTimer.start();
    }
    else
    {
        mEventTimer.stop();
    }

    // The full-size Firestorm reload panel must not draw outside the compact AO.
    // Keep the original reload cover, but display its compact status when collapsed.
    mReloadCoverPanel->getChild<LLPanel>("ao_reload_text_panel")->setVisible(reload && mMore);
    mReloadCoverPanel->getChild<LLTextBox>("ao_reload_status_small")->setVisible(reload && !mMore);
    mReloadCoverPanel->setVisible(reload);
    enableSetControls(!reload);
    enableStateControls(!reload);
}

bool FloaterAO::tick()
{
    // reloading took too long, probably missed the signal, so we hide the reload cover
    llwarns << "AO reloading timeout." << llendl;
    updateList();
    return false;
}

void FloaterAO::updateSetParameters()
{
    mOverrideSitsCheckBox->setValue(mSelectedSet->getSitOverride());
    mOverrideSitsCheckBoxSmall->setValue(mSelectedSet->getSitOverride());
    mSmartCheckBox->setValue(mSelectedSet->getSmart());
    mDisableMouselookCheckBox->setValue(mSelectedSet->getMouselookStandDisable());
    bool isDefault = (mSelectedSet == AOEngine::instance().getDefaultSet());
    mDefaultCheckBox->setValue(isDefault);
    updateSmart();
}

void FloaterAO::updateAnimationList()
{
    S32 currentStateSelected = mStateSelector->getCurrentIndex();

    mStateSelector->removeall();
    onChangeAnimationSelection();

    if (!mSelectedSet)
    {
        mStateSelector->setEnabled(false);
        mStateSelector->add(getString("ao_no_animations_loaded"));
        return;
    }

    for (auto index = 0; index < mSelectedSet->mStateNames.size(); ++index)
    {
        const std::string& stateName = mSelectedSet->mStateNames[index];
        AOSet::AOState* state = mSelectedSet->getStateByName(stateName);
        mStateSelector->add(stateName, state, ADD_BOTTOM, true);
    }

    enableStateControls(true);

    if (currentStateSelected == -1)
    {
        mStateSelector->selectFirstItem();
    }
    else
    {
        mStateSelector->selectNthItem(currentStateSelected);
    }

    onSelectState();
}

void FloaterAO::updateList()
{
    mReloadButton->setEnabled(true);
    mImportRunning = false;
    mSelectedSet = nullptr;
    mSelectedState = nullptr;

    // Lambda provides simple Alpha sorting, note this is case sensitive.
    auto sortRuleLambda = [](const AOSet* s1, const AOSet* s2) -> bool
    {
        return s1->getName() < s2->getName();
    };

    mSetList = AOEngine::instance().getSetList();
    std::sort(mSetList.begin(), mSetList.end(), sortRuleLambda);

    // remember currently selected animation set name
    std::string currentSetName = mSetSelector->getSelectedItemLabel();

    mSetSelector->removeall();
    mSetSelectorSmall->removeall();
    mSetSelector->clear();
    mSetSelectorSmall->clear();

    mAnimationList->deleteAllItems();
    mCurrentBoldItem = nullptr;
    reloading(false);

    static std::string no_sets_label = getString("ao_no_sets_loaded");
    if (mSetList.empty())
    {
        LL_DEBUGS("AOEngine") << "empty set list" << llendl;
        mSetSelector->add(no_sets_label);
        mSetSelectorSmall->add(no_sets_label);
        mSetSelector->selectNthItem(0);
        mSetSelectorSmall->selectNthItem(0);
        enableSetControls(false);
        return;
    }

    // make sure we have an animation set name to display
    if (currentSetName.empty() || currentSetName == no_sets_label)
    {
        // selected animation set was empty, get the currently active animation set from the engine
        currentSetName = AOEngine::instance().getCurrentSetName();
        LL_DEBUGS("AOEngine") << "Current set name was empty, fetched name \"" << currentSetName << "\" from AOEngine" << llendl;

        if (currentSetName.empty())
        {
            // selected animation set was empty, get the name of the first animation set in the list
            currentSetName = mSetList[0]->getName();
            LL_DEBUGS("AOEngine") << "Current set name still empty, fetched first set's name \"" << currentSetName << "\"" << llendl;
        }
    }

    size_t selected_index = 0;
    for (auto index = 0; index < mSetList.size(); ++index)
    {
        std::string setName = mSetList[index]->getName();
        mSetSelector->add(setName, &mSetList[index], ADD_BOTTOM, true);
        mSetSelectorSmall->add(setName, &mSetList[index], ADD_BOTTOM, true);
        if (setName.compare(currentSetName) == 0)
        {
            selected_index = index;
            mSelectedSet = AOEngine::instance().selectSetByName(currentSetName);
            updateSetParameters();
            updateAnimationList();
        }
    }

    mSetSelector->selectNthItem(static_cast<S32>(selected_index));
    mSetSelectorSmall->selectNthItem(static_cast<S32>(selected_index));

    enableSetControls(true);
    if (mSetSelector->getSelectedItemLabel().empty())
    {
        onClickReload();
    }
}

LLSD FloaterAO::encodeRowValue(const RowInfo& info)
{
    return LLSD(llformat("%s|%d|%d", info.mKind.c_str(), info.mIndex, info.mMember));
}

bool FloaterAO::getRowInfo(LLScrollListItem* item, RowInfo& info) const
{
    if (!item)
    {
        return false;
    }

    std::vector<std::string> tokens;
    LLStringUtil::getTokens(item->getValue().asString(), tokens, "|");
    if (tokens.size() != 3)
    {
        return false;
    }

    info.mKind = tokens[0];
    if (!LLStringUtil::convertToS32(tokens[1], info.mIndex) || !LLStringUtil::convertToS32(tokens[2], info.mMember))
    {
        return false;
    }
    return true;
}

LLScrollListItem* FloaterAO::addRow(LLScrollListCtrl* list, const std::string& icon, const std::string& name, const RowInfo& info, const std::string& tooltip, bool indented, bool bold)
{
    LLSD row;
    row["value"] = encodeRowValue(info);

    row["columns"][0]["column"] = "icon";
    row["columns"][0]["type"] = "icon";
    row["columns"][0]["value"] = icon;

    row["columns"][1]["column"] = "animation_name";
    row["columns"][1]["type"] = "text";
    row["columns"][1]["value"] = indented ? "      " + name : name;

    LLScrollListItem* item = list->addElement(row);
    if (item)
    {
        if (bold)
        {
            if (LLScrollListText* text = dynamic_cast<LLScrollListText*>(item->getColumn(1)))
            {
                text->setFontStyle(LLFontGL::BOLD);
            }
        }
    }
    return item;
}

// expansion memory is per set+state, for this floater session only
std::string FloaterAO::expansionKey() const
{
    if (!mSelectedSet || !mSelectedState)
    {
        return std::string();
    }
    return mSelectedSet->getName() + "|" + mSelectedState->mName;
}

bool FloaterAO::isFolderExpanded(const LLUUID& folderID) const
{
    auto found = mExpandedFolders.find(expansionKey());
    if (found == mExpandedFolders.end())
    {
        return false;
    }
    return found->second.count(folderID) > 0;
}

void FloaterAO::setFolderExpanded(const LLUUID& folderID, bool expanded)
{
    if (expanded)
    {
        mExpandedFolders[expansionKey()].insert(folderID);
    }
    else
    {
        mExpandedFolders[expansionKey()].erase(folderID);
    }
}

void FloaterAO::requestListRebuild()
{
    if (mRebuildPending)
    {
        return;
    }
    mRebuildPending = true;
    LLHandle<LLFloater> handle = getHandle();
    doOnIdleOneTime([handle]()
    {
        if (FloaterAO* self = dynamic_cast<FloaterAO*>(handle.get()))
        {
            self->mRebuildPending = false;
            self->rebuildAnimationList();
        }
    });
}

bool FloaterAO::postBuild()
{
    LLPanel* aoPanel = getChild<LLPanel>("animation_overrider_outer_panel");
    mMainInterfacePanel = aoPanel->getChild<LLPanel>("animation_overrider_panel");
    mSmallInterfacePanel = aoPanel->getChild<LLPanel>("animation_overrider_panel_small");
    mReloadCoverPanel = aoPanel->getChild<LLPanel>("ao_reload_cover");

    mSetSelector = mMainInterfacePanel->getChild<LLComboBox>("ao_set_selection_combo");
    mActivateSetButton = mMainInterfacePanel->getChild<LLButton>("ao_activate");
    mAddButton = mMainInterfacePanel->getChild<LLButton>("ao_add");
    mRemoveButton = mMainInterfacePanel->getChild<LLButton>("ao_remove");
    mDefaultCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_default");
    mOverrideSitsCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_sit_override");
    mSmartCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_smart");
    mDisableMouselookCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_disable_stands_in_mouselook");

    mStateSelector = mMainInterfacePanel->getChild<LLComboBox>("ao_state_selection_combo");
    mAnimationList = mMainInterfacePanel->getChild<LLScrollListCtrl>("ao_state_animation_list");
    mMoveUpButton = mMainInterfacePanel->getChild<LLButton>("ao_move_up");
    mMoveDownButton = mMainInterfacePanel->getChild<LLButton>("ao_move_down");
    mRenameButton = mMainInterfacePanel->getChild<LLButton>("ao_rename");
    mTrashButton = mMainInterfacePanel->getChild<LLButton>("ao_trash");
    mCycleCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_cycle");
    mRandomizeCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_randomize");
    mCycleTimeTextLabel = mMainInterfacePanel->getChild<LLTextBox>("ao_cycle_time_seconds_label");
    mCycleTimeSpinner = mMainInterfacePanel->getChild<LLSpinCtrl>("ao_cycle_time");

    mTrackSelector = mMainInterfacePanel->getChild<LLComboBox>("ao_track_selection_combo");
    mTrackAddButton = mMainInterfacePanel->getChild<LLButton>("ao_track_add");
    mDropZonePanel = mMainInterfacePanel->getChild<LLPanel>("ao_drop_zone_panel");

    mReloadButton = mMainInterfacePanel->getChild<LLButton>("ao_reload");
    mPreviousButton = mMainInterfacePanel->getChild<LLButton>("ao_previous");
    mNextButton = mMainInterfacePanel->getChild<LLButton>("ao_next");
    mLessButton = mMainInterfacePanel->getChild<LLButton>("ao_less");

    mSetSelectorSmall = mSmallInterfacePanel->getChild<LLComboBox>("ao_set_selection_combo_small");
    mMoreButton = mSmallInterfacePanel->getChild<LLButton>("ao_more");
    mPreviousButtonSmall = mSmallInterfacePanel->getChild<LLButton>("ao_previous_small");
    mNextButtonSmall = mSmallInterfacePanel->getChild<LLButton>("ao_next_small");
    mOverrideSitsCheckBoxSmall = mSmallInterfacePanel->getChild<LLCheckBoxCtrl>("ao_sit_override_small");

    mSetSelector->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onSelectSet>);

    mSetSelector->setCallbackUserData(this);
    mSetSelector->setFocusLostCallback(&FloaterAO::onFocusLost, this);
    mActivateSetButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickActivate>);
    mActivateSetButton->setCallbackUserData(this);
    buildAddMenu();
    mRemoveButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickRemove>);
    mRemoveButton->setCallbackUserData(this);
    mDefaultCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckDefault>);
    mDefaultCheckBox->setCallbackUserData(this);
    mOverrideSitsCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckOverrideSits>);
    mOverrideSitsCheckBox->setCallbackUserData(this);
    mSmartCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckSmart>);
    mSmartCheckBox->setCallbackUserData(this);
    mDisableMouselookCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckDisableStands>);
    mDisableMouselookCheckBox->setCallbackUserData(this);

    mAnimationList->setCommitOnSelectionChange(true);

    mStateSelector->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onSelectState>);

    mStateSelector->setCallbackUserData(this);
    mAnimationList->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onChangeAnimationSelection>);
    mAnimationList->setCallbackUserData(this);
    mMoveUpButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickMoveUp>);
    mMoveUpButton->setCallbackUserData(this);
    mMoveDownButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickMoveDown>);
    mMoveDownButton->setCallbackUserData(this);
    mRenameButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickRename>);
    mRenameButton->setCallbackUserData(this);
    mTrashButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickTrash>);
    mTrashButton->setCallbackUserData(this);
    mCycleCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckCycle>);
    mCycleCheckBox->setCallbackUserData(this);
    mRandomizeCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckRandomize>);
    mRandomizeCheckBox->setCallbackUserData(this);
    mCycleTimeSpinner->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onChangeCycleTime>);
    mCycleTimeSpinner->setCallbackUserData(this);

    mAnimationList->setRightMouseDownCallback(boost::bind(&FloaterAO::onAnimationListRightClick, this,
        boost::placeholders::_1, boost::placeholders::_2, boost::placeholders::_3, boost::placeholders::_4));
    mTrackSelector->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onSelectTrackTab>);
    mTrackSelector->setCallbackUserData(this);
    mTrackAddButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickAddTrack>);
    mTrackAddButton->setCallbackUserData(this);

    mReloadButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickReload>);

    mReloadButton->setCallbackUserData(this);
    mPreviousButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickPrevious>);
    mPreviousButton->setCallbackUserData(this);
    mNextButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickNext>);
    mNextButton->setCallbackUserData(this);
    mLessButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickLess>);
    mLessButton->setCallbackUserData(this);
    mOverrideSitsCheckBoxSmall->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckOverrideSitsSmall>);
    mOverrideSitsCheckBoxSmall->setCallbackUserData(this);

    mSetSelectorSmall->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onSelectSetSmall>);

    mSetSelectorSmall->setCallbackUserData(this);
    mMoreButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickMore>);
    mMoreButton->setCallbackUserData(this);
    mPreviousButtonSmall->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickPrevious>);
    mPreviousButtonSmall->setCallbackUserData(this);
    mNextButtonSmall->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickNext>);
    mNextButtonSmall->setCallbackUserData(this);

    mAnimationList->setDoubleClickCallback(&FloaterAO::onDoubleClickStatic);

    updateSmart();

    AOEngine::instance().setReloadCallback(boost::bind(&FloaterAO::updateList, this));
    AOEngine::instance().setAnimationChangedCallback(boost::bind(&FloaterAO::onAnimationChanged, this, boost::placeholders::_1));

    onChangeAnimationSelection();
    mMainInterfacePanel->setVisible(true);
    mSmallInterfacePanel->setVisible(false);
    reloading(true);

    updateList();

    if (gSavedPerAccountSettings.getBool("UseFullAOInterface"))
    {
        onClickMore();
    }
    else
    {
        onClickLess();
    }

    return LLFloater::postBuild();
}

void FloaterAO::enableSetControls(bool enable)
{
    mSetSelector->setEnabled(enable);
    mSetSelectorSmall->setEnabled(enable);
    mActivateSetButton->setEnabled(enable);
    mRemoveButton->setEnabled(enable);
    mDefaultCheckBox->setEnabled(enable);
    mOverrideSitsCheckBox->setEnabled(enable);
    mOverrideSitsCheckBoxSmall->setEnabled(enable);
    mDisableMouselookCheckBox->setEnabled(enable);

    if (!enable)
    {
        enableStateControls(enable);
    }
}

void FloaterAO::enableStateControls(bool enable)
{
    mStateSelector->setEnabled(enable);
    mAnimationList->setEnabled(enable);
    mCycleCheckBox->setEnabled(enable);
    if (enable)
    {
        updateCycleParameters();
    }
    else
    {
        mRandomizeCheckBox->setEnabled(enable);
        mCycleTimeTextLabel->setEnabled(enable);
        mCycleTimeSpinner->setEnabled(enable);
    }
    mTrackSelector->setEnabled(enable);
    mTrackAddButton->setEnabled(enable && mSelectedState && mSelectedState->mInventoryUUID.notNull());
    mPreviousButton->setEnabled(enable);
    mPreviousButtonSmall->setEnabled(enable);
    mNextButton->setEnabled(enable);
    mNextButtonSmall->setEnabled(enable);
    mCanDragAndDrop = enable;
}

void FloaterAO::onOpen()
{
    
}

void FloaterAO::onClose(bool app_quitting)
{
    // The older Chaos LLFloater::close() calls onClose() for cleanup but
    // does not hide the window itself. Preserve the AO singleton for reopening.
    setVisible(false);
}

void FloaterAO::onSelectSet()
{
    AOSet* set = AOEngine::instance().getSetByName(mSetSelector->getSelectedItemLabel());
    if (!set)
    {
        onRenameSet();
        return;
    }

    // only update the interface when we actually selected a different set - FIRE-29542
    if (mSelectedSet != set)
    {
        mSelectedSet = set;

        updateSetParameters();
        updateAnimationList();
    }
}

void FloaterAO::onSelectSetSmall()
{
    // sync main set selector with small set selector
    mSetSelector->selectNthItem(mSetSelectorSmall->getCurrentIndex());

    mSelectedSet = AOEngine::instance().getSetByName(mSetSelectorSmall->getSelectedItemLabel());
    if (mSelectedSet)
    {
        updateSetParameters();
        updateAnimationList();

        // small selector activates the selected set immediately
        onClickActivate();
    }
}

void FloaterAO::onRenameSet()
{
    if (!mSelectedSet)
    {
        llwarns << "Rename AO set without set selected." << llendl;
        return;
    }

    std::string name = mSetSelector->getSimple();
    LLStringUtil::trim(name);

    LLUIString new_set_name = name;

    if (!name.empty())
    {
        if (
            validAOName(new_set_name.getWString()) && // only allow ASCII
            name.find_first_of(":|") == std::string::npos)                              // don't allow : or |
        {
            if (AOEngine::instance().renameSet(mSelectedSet, name))
            {
                reloading(true);
                return;
            }
        }
        else
        {
            LLSD args;
            args["AO_SET_NAME"] = name;
            gNotifications.add("RenameAOMustBeASCII", args);
        }
    }
    mSetSelector->setSimple(mSelectedSet->getName());
}

void FloaterAO::onClickActivate()
{
    // sync small set selector with main set selector
    mSetSelectorSmall->selectNthItem(mSetSelector->getCurrentIndex());

    LL_DEBUGS("AOEngine") << "Set activated: " << mSetSelector->getSelectedItemLabel() << llendl;
    AOEngine::instance().selectSet(mSelectedSet);
}

void FloaterAO::rebuildAnimationList()
{
    S32 scrollPos = mAnimationList->getScrollPos();
    LLSD selectedValue;
    if (LLScrollListItem* selected = mAnimationList->getFirstSelected())
    {
        selectedValue = selected->getValue();
    }

    mAnimationList->deleteAllItems();
    mCurrentBoldItem = nullptr;

    if (!mSelectedSet || !mSelectedState)
    {
        return;
    }

    if (mCurrentTrack > 0)
    {
        if (mCurrentTrack > (S32)mSelectedState->mTracks.size())
        {
            mCurrentTrack = 0;
        }
        else
        {
            const AOSet::AOTrack& track = mSelectedState->mTracks[mCurrentTrack - 1];
            for (S32 memberIndex = 0; memberIndex < (S32)track.mAnimations.size(); ++memberIndex)
            {
                bool isPlaying = track.mCurrentAnimationID.notNull() && ((S32)track.mCurrentAnimation == memberIndex);
                RowInfo info;
                info.mKind = "trackmember";
                info.mIndex = mCurrentTrack - 1;
                info.mMember = memberIndex;
                LLScrollListItem* item = addRow(mAnimationList, isPlaying ? "FSAO_Animation_Playing" : "FSAO_Animation_Stopped", track.mAnimations[memberIndex].mName, info, "", false, isPlaying);
                if (isPlaying)
                {
                    mCurrentBoldItem = item;
                }
            }

            if (!track.mAnimations.empty())
            {
            }

            if (selectedValue.isDefined())
            {
                mAnimationList->setSelectedByValue(selectedValue, true);
            }
            mAnimationList->setScrollPos(scrollPos);
            return;
        }
    }

    bool stateIsActive = !mSelectedState->mCurrentAnimationIDs.empty() && (mSelectedState->mRemapID.isNull() || mSelectedSet->getMotion() == mSelectedState->mRemapID);

    for (S32 index = 0; index < (S32)mSelectedState->mSteps.size(); ++index)
    {
        const AOSet::AOAnimationStep& step = mSelectedState->mSteps[index];
        bool isPlaying = stateIsActive && ((S32)mSelectedState->mCurrentAnimation == index);

        if (step.mIsGroup)
        {
            bool expanded = isFolderExpanded(step.mInventoryUUID);

            std::string memberNames;
            for (const auto& member : step.mMembers)
            {
                if (!memberNames.empty())
                {
                    memberNames += ", ";
                }
                memberNames += member.mName;
            }

            RowInfo info;
            info.mKind = "group";
            info.mIndex = index;

            LLScrollListItem* item = addRow(mAnimationList, isPlaying ? "FSAO_Animation_Playing" : (expanded ? "Inv_FolderOpen" : "Inv_FolderClosed"),
                (expanded ? "[-] " : "[+] ") + step.mName, info, getString("ao_group_tooltip") + " " + memberNames, false, isPlaying);

            if (isPlaying)
            {
                mCurrentBoldItem = item;
            }

            if (expanded)
            {
                for (S32 memberIndex = 0; memberIndex < (S32)step.mMembers.size(); ++memberIndex)
                {
                    RowInfo memberInfo;
                    memberInfo.mKind = "member";
                    memberInfo.mIndex = index;
                    memberInfo.mMember = memberIndex;
                    addRow(mAnimationList, isPlaying ? "FSAO_Animation_Playing" : "FSAO_Animation_Stopped", step.mMembers[memberIndex].mName, memberInfo, getString("ao_member_tooltip"), true, isPlaying);
                }
            }
        }
        else
        {
            RowInfo info;
            info.mKind = "single";
            info.mIndex = index;

            LLScrollListItem* item = addRow(mAnimationList, isPlaying ? "FSAO_Animation_Playing" : "FSAO_Animation_Stopped", step.mName, info, "", false, isPlaying);

            if (isPlaying)
            {
                mCurrentBoldItem = item;
            }
        }
    }

    if (!mSelectedState->mSteps.empty())
    {
    }

    if (selectedValue.isDefined())
    {
        mAnimationList->setSelectedByValue(selectedValue, true);
    }
    mAnimationList->setScrollPos(scrollPos);
}

void FloaterAO::rebuildTrackSelector()
{
    mTrackSelector->removeall();
    mTrackSelector->add(getString("ao_track_main"), LLSD(0), ADD_BOTTOM, true);

    if (!mSelectedState)
    {
        mCurrentTrack = 0;
    }
    else
    {
        for (S32 index = 0; index < (S32)mSelectedState->mTracks.size(); ++index)
        {
            mTrackSelector->add(mSelectedState->mTracks[index].mName, LLSD(index + 1), ADD_BOTTOM, true);
        }

        if (!mPendingTrackSelection.empty())
        {
            for (S32 index = 0; index < (S32)mSelectedState->mTracks.size(); ++index)
            {
                if (mSelectedState->mTracks[index].mName == mPendingTrackSelection)
                {
                    mCurrentTrack = index + 1;
                    break;
                }
            }
            mPendingTrackSelection.clear();
        }

        if (mCurrentTrack > (S32)mSelectedState->mTracks.size())
        {
            mCurrentTrack = 0;
        }
    }

    mTrackSelector->selectNthItem(mCurrentTrack);
    mTrackAddButton->setEnabled(mCanDragAndDrop && mSelectedState && mSelectedState->mInventoryUUID.notNull());
}

void FloaterAO::onSelectState()
{
    mAnimationList->deleteAllItems();
    mCurrentBoldItem = nullptr;
    mAnimationList->setEnabled(false);

    onChangeAnimationSelection();

    if (!mSelectedSet)
    {
        return;
    }

    mSelectedState = mSelectedSet->getStateByName(mStateSelector->getSelectedItemLabel());
    if (!mSelectedState)
    {
        return;
    }

    mSelectedState = (AOSet::AOState*)mStateSelector->getCurrentUserdata();

    const std::string stateKey = mSelectedSet->getName() + "|" + mSelectedState->mName;
    if (mLastSelectedStateName != stateKey)
    {
        mLastSelectedStateName = stateKey;
        mCurrentTrack = 0;
    }

    rebuildTrackSelector();
    rebuildAnimationList();
    mAnimationList->setEnabled(true);

    updateCycleControlValues();
}

void FloaterAO::onClickReload()
{
    reloading(true);

    mSelectedSet = nullptr;
    mSelectedState = nullptr;

    AOEngine::instance().reload(false);
    updateList();
}

LLMenuItemCallGL* FloaterAO::addMenuItem(LLMenuGL* menu, const std::string& name,
                                       const std::string& label, const std::function<void()>& fn,
                                       bool enabled)
{
    auto action = std::make_unique<MenuAction>();
    action->fn = fn;
    LLMenuItemCallGL* item = new LLMenuItemCallGL(name, label,
        &FloaterAO::menuActionCallback, nullptr, action.get(), KEY_NONE, MASK_NONE, enabled);
    if (menu == mAddMenu) mAddMenuActions.emplace_back(std::move(action));
    else mContextMenuActions.emplace_back(std::move(action));
    menu->append(item);
    return item;
}

void FloaterAO::buildAddMenu()
{
    mAddMenu = new LLMenuGL("ao_add_menu", "AO Add");
    gMenuHolderp->addChild(mAddMenu);
    mAddMenu->setVisible(false);
    addMenuItem(mAddMenu, "ao_add_blank", getString("ao_add_blank_label"),
        [this]() { onAddSet(false); });
    mCloneSetItem = addMenuItem(mAddMenu, "ao_add_clone", getString("ao_add_clone_label"),
        [this]() { onAddSet(true); }, mSelectedSet != nullptr);
    mAddButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onAddMenuShow>);
    mAddButton->setCallbackUserData(this);
}

void FloaterAO::onAddMenuShow()
{
    if (!mAddMenu) return;
    if (mCloneSetItem) mCloneSetItem->setEnabled(mSelectedSet != nullptr);
    LLMenuGL::showPopup(mAddButton, mAddMenu, 0, 0);
}
void FloaterAO::onAddSet(bool clone)
{
    if (clone && !mSelectedSet)
    {
        return;
    }
    LLSD payload;
    payload["clone"] = clone;
    gNotifications.add("NewAOSet", LLSD(), payload, boost::bind(&FloaterAO::newSetCallback, this, boost::placeholders::_1, boost::placeholders::_2));
}

bool FloaterAO::newSetCallback(const LLSD& notification, const LLSD& response)
{
    std::string newSetName = response["message"].asString();
    S32 option = LLNotification::getSelectedOption(notification, response);
    bool clone = notification["payload"]["clone"].asBoolean();

    LLStringUtil::trim(newSetName);

    LLUIString new_set_name = newSetName;

    if (newSetName.empty())
    {
        return false;
    }
    if (
        !validAOName(new_set_name.getWString()) ||        // only allow ASCII
        newSetName.find_first_of(":|") != std::string::npos)                            // don't allow : or |
    {
        LLSD args;
        args["AO_SET_NAME"] = newSetName;
        gNotifications.add("NewAOCantContainNonASCII", args);
        return false;
    }

    if (option == 0)
    {
        if (AOEngine::instance().getSetByName(newSetName))
        {
            gNotifications.add("NewAONameCantExist");
            return false;
        }

        if (clone)
        {
            if (mSelectedSet && AOEngine::instance().cloneSet(mSelectedSet, newSetName))
            {
                reloading(true);
            }
        }
        else
        {
            AOEngine::instance().addSet(newSetName, [this](const LLUUID& new_cat_id)
            {
                reloading(true);
            });
        }
    }
    return false;
}

void FloaterAO::onClickRemove()
{
    if (!mSelectedSet)
    {
        return;
    }

    LLSD args;
    args["AO_SET_NAME"] = mSelectedSet->getName();
    gNotifications.add("RemoveAOSet", args, LLSD(), boost::bind(&FloaterAO::removeSetCallback, this, boost::placeholders::_1, boost::placeholders::_2));
}

bool FloaterAO::removeSetCallback(const LLSD& notification, const LLSD& response)
{
    S32 option = LLNotification::getSelectedOption(notification, response);

    if (option ==0)
    {
        if (AOEngine::instance().removeSet(mSelectedSet))
        {
            reloading(true);
            // to prevent snapping back to deleted set
            mSetSelector->removeall();
            mSetSelectorSmall->removeall();
            // visually indicate there are no items left
            mSetSelector->clear();
            mSetSelectorSmall->clear();
            mAnimationList->deleteAllItems();
            mCurrentBoldItem = nullptr;
            return true;
        }
    }
    return false;
}

void FloaterAO::onCheckDefault()
{
    if (mSelectedSet)
    {
        AOSet* selectedSet = mDefaultCheckBox->getValue().asBoolean() ? mSelectedSet : nullptr;
        AOEngine::instance().setDefaultSet(selectedSet);
    }
}

void FloaterAO::onCheckOverrideSits()
{
    mOverrideSitsCheckBoxSmall->setValue(mOverrideSitsCheckBox->getValue());
    if (mSelectedSet)
    {
        AOEngine::instance().setOverrideSits(mSelectedSet, mOverrideSitsCheckBox->getValue().asBoolean());
    }
    updateSmart();
}

void FloaterAO::onCheckOverrideSitsSmall()
{
    mOverrideSitsCheckBox->setValue(mOverrideSitsCheckBoxSmall->getValue());
    onCheckOverrideSits();
}

void FloaterAO::updateSmart()
{
    mSmartCheckBox->setEnabled(mOverrideSitsCheckBox->getValue());
}

void FloaterAO::onCheckSmart()
{
    if (mSelectedSet)
    {
        AOEngine::instance().setSmart(mSelectedSet, mSmartCheckBox->getValue().asBoolean());
    }
}

void FloaterAO::onCheckDisableStands()
{
    if (mSelectedSet)
    {
        AOEngine::instance().setDisableMouselookStands(mSelectedSet, mDisableMouselookCheckBox->getValue().asBoolean());
    }
}

void FloaterAO::onChangeAnimationSelection()
{
    std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
    LL_DEBUGS("AOEngine") << "Selection count: " << list.size() << llendl;

    bool resortEnable = false;
    bool trashEnable = false;
    bool renameEnable = false;

    std::string selectedRowValue;
    if (list.size() == 1)
    {
        selectedRowValue = list[0]->getValue().asString();
    }
    bool selectionTransition = (selectedRowValue != mLastSelectedRowValue);
    mLastSelectedRowValue = selectedRowValue;

    // Linden Lab bug: scroll lists still select the first item when you click on them, even when they are disabled.
    // The control does not memorize it's enabled/disabled state, so mAnimationList->mEnabled() doesn't seem to work.
    // So we need to safeguard against it.
    if (!mCanDragAndDrop)
    {
        mAnimationList->deselectAllItems();
        mLastSelectedRowValue.clear();
        LL_DEBUGS("AOEngine") << "Selection count now: " << list.size() << llendl;
    }
    else if (!list.empty())
    {
        if (list.size() == 1)
        {
            resortEnable = true;
            RowInfo info;
            if (mSelectedState && getRowInfo(list[0], info) && info.mKind == "group")
            {
                renameEnable = true;
                if (selectionTransition && info.mIndex < (S32)mSelectedState->mSteps.size() && !isFolderExpanded(mSelectedState->mSteps[info.mIndex].mInventoryUUID))
                {
                    setFolderExpanded(mSelectedState->mSteps[info.mIndex].mInventoryUUID, true);
                    requestListRebuild();
                }
            }
        }
        trashEnable = true;
    }
    else if (mCurrentTrack > 0 && mSelectedState && mCurrentTrack <= (S32)mSelectedState->mTracks.size() && mSelectedState->mTracks[mCurrentTrack - 1].mAnimations.empty())
    {
        trashEnable = true;
    }

    mMoveDownButton->setEnabled(resortEnable);
    mMoveUpButton->setEnabled(resortEnable);
    mRenameButton->setEnabled(renameEnable);
    mTrashButton->setEnabled(trashEnable);
}

void FloaterAO::onClickMoveUp()
{
    if (!mSelectedState)
    {
        return;
    }

    std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
    if (list.size() != 1)
    {
        return;
    }

    RowInfo info;
    if (!getRowInfo(list[0], info))
    {
        return;
    }

    if (info.mKind == "trackmember")
    {
        if (AOEngine::instance().swapTrackAnimationWithPrevious(mSelectedState, info.mIndex, info.mMember))
        {
            RowInfo newInfo = info;
            newInfo.mMember--;
            rebuildAnimationList();
            mAnimationList->setSelectedByValue(encodeRowValue(newInfo), true);
        }
        return;
    }

    S32 memberIndex = (info.mKind == "member") ? info.mMember : -1;
    if (AOEngine::instance().swapWithPrevious(mSelectedState, info.mIndex, memberIndex))
    {
        RowInfo newInfo = info;
        if (memberIndex == -1)
        {
            newInfo.mIndex--;
        }
        else
        {
            newInfo.mMember--;
        }
        rebuildAnimationList();
        mAnimationList->setSelectedByValue(encodeRowValue(newInfo), true);
    }
}

void FloaterAO::onClickMoveDown()
{
    if (!mSelectedState)
    {
        return;
    }

    std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
    if (list.size() != 1)
    {
        return;
    }

    RowInfo info;
    if (!getRowInfo(list[0], info))
    {
        return;
    }

    if (info.mKind == "trackmember")
    {
        if (AOEngine::instance().swapTrackAnimationWithNext(mSelectedState, info.mIndex, info.mMember))
        {
            RowInfo newInfo = info;
            newInfo.mMember++;
            rebuildAnimationList();
            mAnimationList->setSelectedByValue(encodeRowValue(newInfo), true);
        }
        return;
    }

    S32 memberIndex = (info.mKind == "member") ? info.mMember : -1;
    if (AOEngine::instance().swapWithNext(mSelectedState, info.mIndex, memberIndex))
    {
        RowInfo newInfo = info;
        if (memberIndex == -1)
        {
            newInfo.mIndex++;
        }
        else
        {
            newInfo.mMember++;
        }
        rebuildAnimationList();
        mAnimationList->setSelectedByValue(encodeRowValue(newInfo), true);
    }
}

void FloaterAO::onClickTrash()
{
    if (!mSelectedState)
    {
        return;
    }

    std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();

    if (mCurrentTrack > 0)
    {
        S32 trackIndex = mCurrentTrack - 1;
        if (trackIndex >= (S32)mSelectedState->mTracks.size())
        {
            return;
        }

        if (list.empty())
        {
            if (mSelectedState->mTracks[trackIndex].mAnimations.empty())
            {
                AOEngine::instance().removeTrack(mSelectedState, trackIndex);
                mCurrentTrack = 0;
                rebuildTrackSelector();
                rebuildAnimationList();
                onChangeAnimationSelection();
            }
            return;
        }

        std::vector<S32> members;
        for (LLScrollListItem* item : list)
        {
            RowInfo info;
            if (getRowInfo(item, info) && info.mKind == "trackmember")
            {
                members.push_back(info.mMember);
            }
        }
        std::sort(members.rbegin(), members.rend());

        for (S32 memberIndex : members)
        {
            AOEngine::instance().removeTrackAnimation(mSelectedState, trackIndex, memberIndex);
        }

        if (trackIndex >= (S32)mSelectedState->mTracks.size())
        {
            mCurrentTrack = 0;
        }

        mAnimationList->deselectAllItems();
        mCurrentBoldItem = nullptr;
        rebuildTrackSelector();
        rebuildAnimationList();
        onChangeAnimationSelection();
        return;
    }

    if (list.empty())
    {
        return;
    }

    std::vector<RowInfo> rows;
    std::set<S32> wholeSteps;
    for (LLScrollListItem* item : list)
    {
        RowInfo info;
        if (getRowInfo(item, info))
        {
            rows.emplace_back(info);
            if (info.mKind != "member")
            {
                wholeSteps.insert(info.mIndex);
            }
        }
    }

    rows.erase(std::remove_if(rows.begin(), rows.end(), [&wholeSteps](const RowInfo& info)
    {
        return info.mKind == "member" && wholeSteps.count(info.mIndex) > 0;
    }), rows.end());

    // bottom-up: higher step index first, higher member index before lower
    std::sort(rows.begin(), rows.end(), [](const RowInfo& a, const RowInfo& b)
    {
        if (a.mIndex != b.mIndex)
        {
            return a.mIndex > b.mIndex;
        }
        return a.mMember > b.mMember;
    });

    for (const RowInfo& info : rows)
    {
        S32 memberIndex = (info.mKind == "member") ? info.mMember : -1;
        AOEngine::instance().removeAnimation(mSelectedSet, mSelectedState, info.mIndex, memberIndex);
    }

    mAnimationList->deselectAllItems();
    mCurrentBoldItem = nullptr;
    rebuildAnimationList();
    onChangeAnimationSelection();
}

void FloaterAO::updateCycleParameters()
{
    bool enabled = mCycleCheckBox->getValue().asBoolean();
    mRandomizeCheckBox->setEnabled(enabled);
    mCycleTimeTextLabel->setEnabled(enabled);
    mCycleTimeSpinner->setEnabled(enabled);
}

void FloaterAO::updateCycleControlValues()
{
    if (!mSelectedState)
    {
        return;
    }

    if (mCurrentTrack > 0 && mCurrentTrack <= (S32)mSelectedState->mTracks.size())
    {
        const AOSet::AOTrack& track = mSelectedState->mTracks[mCurrentTrack - 1];
        mCycleCheckBox->setValue(track.mCycle);
        mRandomizeCheckBox->setValue(track.mRandom);
        mCycleTimeSpinner->setValue(track.mCycleTime);
    }
    else
    {
        mCycleCheckBox->setValue(mSelectedState->mCycle);
        mRandomizeCheckBox->setValue(mSelectedState->mRandom);
        mCycleTimeSpinner->setValue(mSelectedState->mCycleTime);
    }

    updateCycleParameters();
}

void FloaterAO::onCheckCycle()
{
    if (mSelectedState)
    {
        bool cycle = mCycleCheckBox->getValue().asBoolean();
        if (mCurrentTrack > 0)
        {
            AOEngine::instance().setTrackCycle(mSelectedState, mCurrentTrack - 1, cycle);
        }
        else
        {
            AOEngine::instance().setCycle(mSelectedState, cycle);
        }
        updateCycleParameters();
    }
}

void FloaterAO::onCheckRandomize()
{
    if (mSelectedState)
    {
        bool randomize = mRandomizeCheckBox->getValue().asBoolean();
        if (mCurrentTrack > 0)
        {
            AOEngine::instance().setTrackRandomize(mSelectedState, mCurrentTrack - 1, randomize);
        }
        else
        {
            AOEngine::instance().setRandomize(mSelectedState, randomize);
        }
    }
}

void FloaterAO::onChangeCycleTime()
{
    if (mSelectedState)
    {
        F32 cycleTime = mCycleTimeSpinner->getValue().asReal();
        if (mCurrentTrack > 0)
        {
            AOEngine::instance().setTrackCycleTime(mSelectedState, mCurrentTrack - 1, cycleTime);
        }
        else
        {
            AOEngine::instance().setCycleTime(mSelectedState, cycleTime);
        }
    }
}

void FloaterAO::onClickPrevious()
{
    AOEngine::instance().cycle(AOEngine::CyclePrevious, true);
}

void FloaterAO::onClickNext()
{
    AOEngine::instance().cycle(AOEngine::CycleNext, true);
}

void FloaterAO::onDoubleClick()
{
    LLScrollListItem* item = mAnimationList->getFirstSelected();
    if (!item)
    {
        return;
    }

    RowInfo info;
    if (!getRowInfo(item, info))
    {
        return;
    }

    // do nothing if animation is for a different state than the active state
    if (!mSelectedState || (mSelectedState->mRemapID.notNull() && mSelectedState != AOEngine::instance().getCurrentState()))
    {
        return;
    }

    // activate AO set if necessary
    if (AOEngine::instance().getCurrentSet() != mSelectedSet)
    {
        // sync small set selector with main set selector
        mSetSelectorSmall->selectNthItem(mSetSelector->getCurrentIndex());

        LL_DEBUGS("AOEngine") << "Set activated: " << mSetSelector->getSelectedItemLabel() << llendl;
        AOEngine::instance().selectSet(mSelectedSet);
    }

    if (info.mKind == "trackmember")
    {
        AOEngine::instance().playTrackAnimation(mSelectedState, info.mIndex, info.mMember);
    }
    else if (mSelectedState->mRemapID.isNull())
    {
        AOEngine::instance().playAlwaysAnimation(info.mIndex, info.mKind == "member" ? info.mMember : -1);
    }
    else
    {
        AOEngine::instance().playAnimation(info.mIndex, info.mKind == "member" ? info.mMember : -1);
    }
}

void FloaterAO::onClickRename()
{
    std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
    if (list.size() != 1)
    {
        return;
    }
    RowInfo info;
    if (getRowInfo(list[0], info))
    {
        onRenameRow(info);
    }
}

void FloaterAO::onRenameRow(const RowInfo& info)
{
    if (!mSelectedState)
    {
        return;
    }
    if (info.mKind == "group" && info.mIndex < (S32)mSelectedState->mSteps.size())
    {
        LLSD args;
        args["AO_GROUP_NAME"] = mSelectedState->mSteps[info.mIndex].mName;
        gNotifications.add("RenameAOGroup", args, LLSD(), boost::bind(&FloaterAO::renameGroupCallback, this, boost::placeholders::_1, boost::placeholders::_2, info.mIndex));
    }
}

bool FloaterAO::renameGroupCallback(const LLSD& notification, const LLSD& response, S32 stepIndex)
{
    if (LLNotification::getSelectedOption(notification, response) != 0 || !mSelectedState)
    {
        return false;
    }

    std::string newName = response["message"].asString();
    LLStringUtil::trim(newName);

    LLUIString new_name_check = newName;
    if (newName.empty() || !validAOName(new_name_check.getWString()) || newName.find_first_of(":|") != std::string::npos || !AOEngine::instance().renameGroup(mSelectedState, stepIndex, newName))
    {
        LLSD args;
        args["AO_SET_NAME"] = newName;
        gNotifications.add("RenameAOEntryMustBeASCII", args);
        return false;
    }

    rebuildAnimationList();
    return true;
}

LLUUID FloaterAO::getRowInventoryUUID(const RowInfo& info) const
{
    if (!mSelectedState)
    {
        return LLUUID::null;
    }

    if (info.mKind == "single")
    {
        if (info.mIndex >= 0 && info.mIndex < (S32)mSelectedState->mSteps.size())
        {
            return mSelectedState->mSteps[info.mIndex].mInventoryUUID;
        }
    }
    else if (info.mKind == "member")
    {
        if (info.mIndex >= 0 && info.mIndex < (S32)mSelectedState->mSteps.size())
        {
            const AOSet::AOAnimationStep& step = mSelectedState->mSteps[info.mIndex];
            if (info.mMember >= 0 && info.mMember < (S32)step.mMembers.size())
            {
                return step.mMembers[info.mMember].mInventoryUUID;
            }
        }
    }
    else if (info.mKind == "trackmember")
    {
        if (info.mIndex >= 0 && info.mIndex < (S32)mSelectedState->mTracks.size())
        {
            const AOSet::AOTrack& track = mSelectedState->mTracks[info.mIndex];
            if (info.mMember >= 0 && info.mMember < (S32)track.mAnimations.size())
            {
                return track.mAnimations[info.mMember].mInventoryUUID;
            }
        }
    }

    return LLUUID::null;
}

void FloaterAO::onAnimationListRightClick(LLUICtrl* ctrl, S32 x, S32 y, MASK mask)
{
    if (!mCanDragAndDrop)
    {
        return;
    }
    mAnimationList->selectItemAt(x, y, MASK_NONE);
    showContextMenu(mAnimationList, x, y);
}

void FloaterAO::showContextMenu(LLScrollListCtrl* list, S32 x, S32 y)
{
    RowInfo info;
    if (!mSelectedState || !getRowInfo(list->getFirstSelected(), info)) return;
    if (mContextMenu) { mContextMenu->die(); mContextMenu = nullptr; }
    mContextMenuActions.clear();

    // Native Chaos LLMenuGL hosts the original Firestorm row actions.
    LLMenuGL* menu = new LLMenuGL("ao_row_context_menu", "Animation");
    mContextMenu = menu;
    gMenuHolderp->addChild(menu);
    menu->setVisible(false);
    if (info.mKind == "group")
    {
        const LLUUID folderID = info.mIndex < (S32)mSelectedState->mSteps.size() ?
            mSelectedState->mSteps[info.mIndex].mInventoryUUID : LLUUID::null;
        addMenuItem(menu, "ao_cm_expand", isFolderExpanded(folderID) ? getString("ao_cm_collapse") : getString("ao_cm_expand"),
            [this, info]() { onToggleRowExpand(info); });
        addMenuItem(menu, "ao_cm_rename", getString("ao_cm_rename_group"),
            [this, info]() { onRenameRow(info); });
    }
    else if (info.mKind == "member")
    {
        addMenuItem(menu, "ao_cm_move_out", getString("ao_cm_move_out"),
            [this, info]() { onExtractMember(info); });
    }

    const LLUUID inventoryID = getRowInventoryUUID(info);
    if (inventoryID.notNull())
    {
        const LLInventoryObject* obj = gInventory.getObject(inventoryID);
        const bool findEnabled = obj && obj->getIsLinkType() && gInventory.getItem(gInventory.getLinkedItemID(inventoryID));
        addMenuItem(menu, "ao_cm_find_original", getString("ao_cm_find_original"),
            [inventoryID]()
            {
                const LLUUID original = gInventory.getLinkedItemID(inventoryID);
                if (original.isNull()) return;
                if (LLFloaterInventory* floater = LLFloaterInventory::showAgentInventory())
                {
                    if (LLInventoryPanel* panel = floater->getPanel()) panel->setSelection(original, true);
                }
            }, findEnabled);
    }
    addMenuItem(menu, "ao_cm_delete", getString("ao_cm_delete"),
        [this, info]() { onDeleteRow(info); });
    LLMenuGL::showPopup(list, menu, x, y);
}
void FloaterAO::onToggleRowExpand(const RowInfo& info)
{
    if (!mSelectedState)
    {
        return;
    }

    LLUUID folderID;
    if (info.mKind == "group" && info.mIndex < (S32)mSelectedState->mSteps.size())
    {
        folderID = mSelectedState->mSteps[info.mIndex].mInventoryUUID;
    }

    if (folderID.notNull())
    {
        setFolderExpanded(folderID, !isFolderExpanded(folderID));
        requestListRebuild();
    }
}

void FloaterAO::onExtractMember(const RowInfo& info)
{
    if (!mSelectedState || info.mKind != "member")
    {
        return;
    }
    AOEngine::instance().extractMemberFromGroup(mSelectedState, info.mIndex, info.mMember);
    reloading(true);
}

void FloaterAO::onDeleteRow(const RowInfo& info)
{
    if (!mSelectedState)
    {
        return;
    }

    if (info.mKind == "trackmember")
    {
        AOEngine::instance().removeTrackAnimation(mSelectedState, info.mIndex, info.mMember);
        if (mCurrentTrack > (S32)mSelectedState->mTracks.size())
        {
            mCurrentTrack = 0;
        }
        mCurrentBoldItem = nullptr;
        rebuildTrackSelector();
        rebuildAnimationList();
        onChangeAnimationSelection();
    }
    else
    {
        S32 memberIndex = (info.mKind == "member") ? info.mMember : -1;
        AOEngine::instance().removeAnimation(mSelectedSet, mSelectedState, info.mIndex, memberIndex);
        mCurrentBoldItem = nullptr;
        rebuildAnimationList();
        onChangeAnimationSelection();
    }
}

void FloaterAO::onSelectTrackTab()
{
    S32 track = mTrackSelector->getSelectedValue().asInteger();
    if (track == mCurrentTrack)
    {
        return;
    }
    mCurrentTrack = track;
    mAnimationList->deselectAllItems();
    mCurrentBoldItem = nullptr;
    rebuildAnimationList();
    updateCycleControlValues();
    onChangeAnimationSelection();
}

void FloaterAO::onClickAddTrack()
{
    if (!mSelectedState || mSelectedState->mInventoryUUID.isNull())
    {
        return;
    }
    std::string trackName = AOEngine::instance().createEmptyTrack(mSelectedState);
    if (!trackName.empty())
    {
        mPendingTrackSelection = trackName;
        reloading(true);
    }
}

void FloaterAO::onClickMore()
{
    LLRect fullSize = gSavedPerAccountSettings.getRect("floater_rect_animation_overrider_full");

    if (fullSize.getHeight() < getMinHeight())
    {
        fullSize.setOriginAndSize(fullSize.mLeft, fullSize.mBottom, fullSize.getWidth(), getRect().getHeight());
    }

    if (fullSize.getWidth() < getMinWidth())
    {
        fullSize.setOriginAndSize(fullSize.mLeft, fullSize.mBottom, getRect().getWidth(), fullSize.getHeight());
    }

    mMore = true;

    mSmallInterfacePanel->setVisible(false);
    mMainInterfacePanel->setVisible(true);
    setCanResize(true);

    gSavedPerAccountSettings.setBool("UseFullAOInterface", true);

    reshape(getRect().getWidth(), fullSize.getHeight());
    mReloadCoverPanel->getChild<LLPanel>("ao_reload_text_panel")->setVisible(mReloadCoverPanel->getVisible());
    mReloadCoverPanel->getChild<LLTextBox>("ao_reload_status_small")->setVisible(false);
}

void FloaterAO::onClickLess()
{
    LLRect fullSize = getRect();
    // The compact panel has a non-zero bottom offset inside the outer panel.
    // Include that offset and the floater chrome so its Edit button stays
    // inside the parent's clickable bounds after the window is collapsed.
    const LLRect outerRect = getChild<LLPanel>("animation_overrider_outer_panel")->getRect();
    const LLRect smallRect = mSmallInterfacePanel->getRect();
    const S32 compactHeight = smallRect.mTop + (fullSize.getHeight() - outerRect.getHeight());

    gSavedPerAccountSettings.setRect("floater_rect_animation_overrider_full", fullSize);

    mMore = false;

    mSmallInterfacePanel->setVisible(true);
    mMainInterfacePanel->setVisible(false);
    setCanResize(false);

    gSavedPerAccountSettings.setBool("UseFullAOInterface", false);

    reshape(getRect().getWidth(), compactHeight);
    mReloadCoverPanel->getChild<LLPanel>("ao_reload_text_panel")->setVisible(false);
    mReloadCoverPanel->getChild<LLTextBox>("ao_reload_status_small")->setVisible(mReloadCoverPanel->getVisible());

    // save current size and position
    gSavedPerAccountSettings.setRect("floater_rect_animation_overrider_full", fullSize);
}

static void setRowPlaying(LLScrollListItem* item, bool playing, const std::string& stoppedIcon)
{
    if (!item)
    {
        return;
    }

    if (LLScrollListIcon* icon = dynamic_cast<LLScrollListIcon*>(item->getColumn(0)))
    {
        icon->setValue(playing ? "FSAO_Animation_Playing" : stoppedIcon);
    }

    if (LLScrollListText* text = dynamic_cast<LLScrollListText*>(item->getColumn(1)))
    {
        text->setFontStyle(playing ? LLFontGL::BOLD : LLFontGL::NORMAL);
    }
}

void FloaterAO::onAnimationChanged(const LLUUID& animation)
{
    LL_DEBUGS("AOEngine") << "Received animation change to " << animation << llendl;

    if (!mAnimationList)
    {
        llwarns << "Animation list control is null." << llendl;
        return;
    }

    mCurrentBoldItem = nullptr;

    if (!mSelectedSet || !mSelectedState)
    {
        return;
    }

    if (mCurrentTrack > 0)
    {
        if (mCurrentTrack > (S32)mSelectedState->mTracks.size())
        {
            return;
        }

        const AOSet::AOTrack& track = mSelectedState->mTracks[mCurrentTrack - 1];
        for (LLScrollListItem* item : mAnimationList->getAllData())
        {
            RowInfo info;
            if (!getRowInfo(item, info) || info.mKind != "trackmember")
            {
                continue;
            }

            bool playing = track.mCurrentAnimationID.notNull() && ((S32)track.mCurrentAnimation == info.mMember);
            setRowPlaying(item, playing, "FSAO_Animation_Stopped");

            if (playing)
            {
                mCurrentBoldItem = item;
            }
        }
        return;
    }

    bool stateIsActive = !mSelectedState->mCurrentAnimationIDs.empty() && (mSelectedState->mRemapID.isNull() || mSelectedSet->getMotion() == mSelectedState->mRemapID);

    for (LLScrollListItem* item : mAnimationList->getAllData())
    {
        RowInfo info;
        if (!getRowInfo(item, info) || info.mIndex < 0 || info.mIndex >= (S32)mSelectedState->mSteps.size())
        {
            continue;
        }

        const AOSet::AOAnimationStep& step = mSelectedState->mSteps[info.mIndex];
        bool playing = stateIsActive && ((S32)mSelectedState->mCurrentAnimation == info.mIndex);

        std::string stoppedIcon = "FSAO_Animation_Stopped";
        if (info.mKind == "group")
        {
            stoppedIcon = isFolderExpanded(step.mInventoryUUID) ? "Inv_FolderOpen" : "Inv_FolderClosed";
        }
        setRowPlaying(item, playing, stoppedIcon);

        if (playing && info.mKind != "member")
        {
            mCurrentBoldItem = item;
        }
    }
}

// virtual
BOOL FloaterAO::handleDragAndDrop(S32 x, S32 y, MASK mask, BOOL drop, EDragAndDropType type, void* data,
                                    EAcceptance* accept, std::string& tooltipMsg)
{
    // no drag & drop on small interface
    if (!mMore)
    {
        tooltipMsg = getString("ao_dnd_only_on_full_interface");
        *accept = ACCEPT_NO;
        return true;
    }

    LLInventoryItem* item = (LLInventoryItem*)data;

    if (type == DAD_NOTECARD)
    {
        if (mImportRunning)
        {
            *accept = ACCEPT_NO;
            return true;
        }
        *accept = ACCEPT_YES_SINGLE;
        if (item && drop)
        {
            if (AOEngine::instance().importNotecard(item))
            {
                reloading(true);
                mReloadButton->setEnabled(false);
                mImportRunning = true;
            }
        }
    }
    else if (type == DAD_ANIMATION)
    {
        if (!drop && (!mSelectedSet || !mSelectedState || !mCanDragAndDrop))
        {
            *accept = ACCEPT_NO;
            return true;
        }
        *accept = ACCEPT_YES_MULTI;

        if (mCurrentTrack == 0 && mDropZonePanel)
        {
            mDropZonePanel->setVisible(true);
            mDropZoneHovered = true;
        }

        if (mCurrentTrack > 0)
        {
            if (!drop)
            {
                tooltipMsg = getString("ao_dnd_add_to_track");
            }
            if (item && drop && mSelectedState && mCurrentTrack <= (S32)mSelectedState->mTracks.size())
            {
                AOEngine::instance().addAnimationToTrack(mSelectedState, mCurrentTrack - 1, item);
                reloading(true);
            }
            return true;
        }

        RowInfo targetInfo;
        bool haveTarget = false;

        S32 list_x, list_y;
        localPointToOtherView(x, y, &list_x, &list_y, mAnimationList);
        if (mAnimationList->isInVisibleChain() && mAnimationList->pointInView(list_x, list_y))
        {
            haveTarget = getRowInfo(mAnimationList->hitItem(list_x, list_y), targetInfo);
        }

        if (!drop && haveTarget)
        {
            tooltipMsg = (targetInfo.mKind == "single") ? getString("ao_dnd_merge") : getString("ao_dnd_add_to_group");
        }

        if (item && drop)
        {
            if (haveTarget)
            {
                if (targetInfo.mKind == "single")
                {
                    AOEngine::instance().createGroupFromMerge(mSelectedState, targetInfo.mIndex, item);
                }
                else
                {
                    AOEngine::instance().addAnimationToGroup(mSelectedState, targetInfo.mIndex, item);
                }
            }
            else
            {
                AOEngine::instance().addAnimation(mSelectedSet, mSelectedState, item);
            }

            // TODO: this would be the right thing to do, but it blocks multi drop
            // before final release this must be resolved
            reloading(true);
        }
    }
    else if (type == DAD_CATEGORY)
    {
        if (!mSelectedSet || !mSelectedState || !mCanDragAndDrop || (mSelectedState->mInventoryUUID.isNull()))
        {
            *accept = ACCEPT_NO;
            return true;
        }
        *accept = ACCEPT_YES_SINGLE;

        bool overTrack = (mCurrentTrack > 0);

        if (!drop)
        {
            tooltipMsg = overTrack ? getString("ao_dnd_folder_pour") : getString("ao_dnd_folder_group");
        }

        if (drop)
        {
            const LLInventoryCategory* category = (LLInventoryCategory*)data;
            bool success = false;

            if (overTrack)
            {
                // pour the folder's direct animations into the current track
                if (category && mCurrentTrack <= (S32)mSelectedState->mTracks.size())
                {
                    LLInventoryModel::cat_array_t* cats;
                    LLInventoryModel::item_array_t* items;
                    gInventory.getDirectDescendentsOf(category->getUUID(), cats, items);
                    if (items)
                    {
                        for (const auto& folderItem : *items)
                        {
                            if (folderItem->getInventoryType() == LLInventoryType::IT_ANIMATION)
                            {
                                AOEngine::instance().addAnimationToTrack(mSelectedState, mCurrentTrack - 1, folderItem);
                                success = true;
                            }
                        }
                    }
                }
            }
            else
            {
                success = AOEngine::instance().createGroupFromFolder(mSelectedState, category);
            }

            if (success)
            {
                reloading(true);
            }
            else
            {
                LLSD args;
                args["NAME"] = category ? category->getName() : LLStringUtil::null;
                gNotifications.add("AOFolderDropNoAnimations", args);
            }
        }
    }
    else
    {
        *accept = ACCEPT_NO;
    }

    return true;
}
