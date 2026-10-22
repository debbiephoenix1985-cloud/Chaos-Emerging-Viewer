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
#include "llcheckboxctrl.h"
#include "llcombobox.h"
#include "llnotify.h"
#include "lluictrlfactory.h"
#include "llscrolllistctrl.h"
#include <algorithm>
#include <boost/bind/bind.hpp>
#include "llspinctrl.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"



// Firestorm AO set-name validation: printable ASCII, excluding the pipe delimiter.
static bool validAOName(const LLWString& value)
{
	return std::all_of(value.begin(), value.end(),
		[](wchar_t c) { return c >= 0x20 && c <= 0x7e && c != L'|'; });
}
FloaterAO::FloaterAO(const LLSD& key)
:	LLFloater(), LLEventTimer(10.f),
	mSetList(0),
	mSelectedSet(0),
	mSelectedState(0),
	mCanDragAndDrop(false),
	mImportRunning(false),
	mCurrentBoldItem(nullptr),
	mMore(true)
{
	mEventTimer.stop();
	LLUICtrlFactory::getInstance()->buildFloater(this, "floater_ao.xml");
}

FloaterAO::~FloaterAO()
{
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

	mReloadCoverPanel->setVisible(reload);
	enableSetControls(!reload);
	enableStateControls(!reload);
}

bool FloaterAO::tick()
{
	// reloading took too long, probably missed the signal, so we hide the reload cover
	llwarns << "AO reloading timeout." << llendl;
	updateList();
	return FALSE;
}

void FloaterAO::updateSetParameters()
{
	mOverrideSitsCheckBox->setValue(mSelectedSet->getSitOverride());
	mOverrideSitsCheckBoxSmall->setValue(mSelectedSet->getSitOverride());
	mSmartCheckBox->setValue(mSelectedSet->getSmart());
	mDisableMouselookCheckBox->setValue(mSelectedSet->getMouselookStandDisable());
	BOOL isDefault = (mSelectedSet == AOEngine::instance().getDefaultSet());
	mDefaultCheckBox->setValue(isDefault);
	mDefaultCheckBox->setEnabled(!isDefault);
	updateSmart();
}

void FloaterAO::updateAnimationList()
{
	S32 currentStateSelected = mStateSelector->getCurrentIndex();

	mStateSelector->removeall();
	onChangeAnimationSelection();

	if (!mSelectedSet)
	{
		mStateSelector->setEnabled(FALSE);
		mStateSelector->add(getString("ao_no_animations_loaded"));
		return;
	}

	for (U32 index = 0; index < mSelectedSet->mStateNames.size(); ++index)
	{
		std::string stateName = mSelectedSet->mStateNames[index];
		AOSet::AOState* state = mSelectedSet->getStateByName(stateName);
		mStateSelector->add(stateName, state, ADD_BOTTOM, TRUE);
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
	mReloadButton->setEnabled(TRUE);
	mImportRunning = false;

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

	if (mSetList.empty())
	{
		LL_DEBUGS("AOEngine") << "empty set list" << llendl;
		mSetSelector->add(getString("ao_no_sets_loaded"));
		mSetSelectorSmall->add(getString("ao_no_sets_loaded"));
		mSetSelector->selectNthItem(0);
		mSetSelectorSmall->selectNthItem(0);
		enableSetControls(FALSE);
		return;
	}

	// make sure we have an animation set name to display
	if (currentSetName.empty())
	{
		// selected animation set was empty, get the currently active animation set from the engine
		currentSetName = AOEngine::instance().getCurrentSetName();
		LL_DEBUGS("AOEngine") << "Current set name was empty, fetched name \"" << currentSetName << "\" from AOEngine" << llendl;

		if(currentSetName.empty())
		{
			// selected animation set was empty, get the name of the first animation set in the list
			currentSetName = mSetList[0]->getName();
			LL_DEBUGS("AOEngine") << "Current set name still empty, fetched first set's name \"" << currentSetName << "\"" << llendl;
		}
	}

	for (U32 index = 0; index < mSetList.size(); ++index)
	{
		std::string setName = mSetList[index]->getName();
		mSetSelector->add(setName, &mSetList[index], ADD_BOTTOM, TRUE);
		mSetSelectorSmall->add(setName, &mSetList[index], ADD_BOTTOM, TRUE);
		if (setName.compare(currentSetName) == 0)
		{
			mSelectedSet = AOEngine::instance().selectSetByName(currentSetName);
			mSetSelector->selectNthItem(index);
			mSetSelectorSmall->selectNthItem(index);
			updateSetParameters();
			updateAnimationList();
		}
	}
	enableSetControls(TRUE);
	if (mSetSelector->getSelectedItemLabel().empty())
	{
		onClickReload();
	}
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
	mTrashButton = mMainInterfacePanel->getChild<LLButton>("ao_trash");
	mCycleCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_cycle");
	mRandomizeCheckBox = mMainInterfacePanel->getChild<LLCheckBoxCtrl>("ao_randomize");
	mCycleTimeTextLabel = mMainInterfacePanel->getChild<LLTextBox>("ao_cycle_time_seconds_label");
	mCycleTimeSpinner = mMainInterfacePanel->getChild<LLSpinCtrl>("ao_cycle_time");

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
	mSetSelector->setFocusLostCallback(&FloaterAO::onSetFocusLost, this);
	mActivateSetButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickActivate>);
	mActivateSetButton->setCallbackUserData(this);
	mAddButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickAdd>);
	mAddButton->setCallbackUserData(this);
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

	mAnimationList->setCommitOnSelectionChange(TRUE);

	mStateSelector->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onSelectState>);
	mStateSelector->setCallbackUserData(this);
	mAnimationList->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onChangeAnimationSelection>);
	mAnimationList->setCallbackUserData(this);
	mMoveUpButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickMoveUp>);
	mMoveUpButton->setCallbackUserData(this);
	mMoveDownButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickMoveDown>);
	mMoveDownButton->setCallbackUserData(this);
	mTrashButton->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onClickTrash>);
	mTrashButton->setCallbackUserData(this);
	mCycleCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckCycle>);
	mCycleCheckBox->setCallbackUserData(this);
	mRandomizeCheckBox->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onCheckRandomize>);
	mRandomizeCheckBox->setCallbackUserData(this);
	mCycleTimeSpinner->setCommitCallback(&FloaterAO::onControl<&FloaterAO::onChangeCycleTime>);
	mCycleTimeSpinner->setCallbackUserData(this);

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

	updateSmart();

	AOEngine::instance().setReloadCallback(boost::bind(&FloaterAO::updateList, this));
	AOEngine::instance().setAnimationChangedCallback(boost::bind(&FloaterAO::onAnimationChanged, this, boost::placeholders::_1));

	onChangeAnimationSelection();
	mMainInterfacePanel->setVisible(TRUE);
	mSmallInterfacePanel->setVisible(FALSE);
	reloading(TRUE);

	updateList();

	// Open the full Firestorm editor, preserving its original More/Less controls.
	onClickMore();

	return LLFloater::postBuild();
}

void FloaterAO::enableSetControls(BOOL enable)
{
	mSetSelector->setEnabled(enable);
	mSetSelectorSmall->setEnabled(enable);
	mActivateSetButton->setEnabled(enable);
	mRemoveButton->setEnabled(enable);
	mDefaultCheckBox->setEnabled(enable && (mSelectedSet != AOEngine::instance().getDefaultSet()));
	mOverrideSitsCheckBox->setEnabled(enable);
	mOverrideSitsCheckBoxSmall->setEnabled(enable);
	mDisableMouselookCheckBox->setEnabled(enable);

	if (!enable)
	{
		enableStateControls(enable);
	}
}

void FloaterAO::enableStateControls(BOOL enable)
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
	mPreviousButton->setEnabled(enable);
	mPreviousButtonSmall->setEnabled(enable);
	mNextButton->setEnabled(enable);
	mNextButtonSmall->setEnabled(enable);
	mCanDragAndDrop = enable;
}

void FloaterAO::onOpen()
{
	// Load AO sets when the user explicitly opens the editor.
	AOEngine::instance().tick();
}

void FloaterAO::onClose(bool app_quitting)
{
	// Legacy LLFloater::close() expects onClose() to hide or destroy it.
	// Keep the real Firestorm editor singleton available for reopening.
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
		mSelectedSet=set;

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
			validAOName(new_set_name.getWString()) &&	// only allow ASCII
			name.find_first_of(":|") == std::string::npos)								// don't allow : or |
		{
			if (AOEngine::instance().renameSet(mSelectedSet, name))
			{
				reloading(TRUE);
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

LLScrollListItem* FloaterAO::addAnimation(const std::string& name)
{
	LLSD row;
	row["columns"][0]["column"] = "icon";
	row["columns"][0]["type"] = "icon";
	row["columns"][0]["value"] = "Inv_Animation";

	row["columns"][1]["column"] = "animation_name";
	row["columns"][1]["type"] = "text";
	row["columns"][1]["value"] = name;

	return mAnimationList->addElement(row);
}

void FloaterAO::onSelectState()
{
	mAnimationList->deleteAllItems();
	mCurrentBoldItem = nullptr;
	mAnimationList->addCommentText(getString("ao_no_animations_loaded"));
	mAnimationList->setEnabled(FALSE);

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
	if (mSelectedState->mAnimations.size())
	{
		// Clear the empty-state comment before adding real animations.
		mAnimationList->deleteAllItems();
		for (U32 index = 0; index < mSelectedState->mAnimations.size(); ++index)
		{
			LLScrollListItem* item = addAnimation(mSelectedState->mAnimations[index].mName);
			if (item)
			{
				item->setUserdata(&mSelectedState->mAnimations[index].mInventoryUUID);
			}
		}

		
		mAnimationList->setEnabled(TRUE);
	}

	mCycleCheckBox->setValue(mSelectedState->mCycle);
	mRandomizeCheckBox->setValue(mSelectedState->mRandom);
	mCycleTimeSpinner->setValue(mSelectedState->mCycleTime);

	updateCycleParameters();
}

void FloaterAO::onClickReload()
{
	reloading(true);

	mSelectedSet = nullptr;
	mSelectedState = nullptr;

	AOEngine::instance().reload(false);
	updateList();
}

void FloaterAO::onClickAdd()
{
	gNotifications.add("NewAOSet", LLSD(), LLSD(), boost::bind(&FloaterAO::newSetCallback, this, boost::placeholders::_1, boost::placeholders::_2));
}

bool FloaterAO::newSetCallback(const LLSD& notification, const LLSD& response)
{
	std::string newSetName = response["message"].asString();
	S32 option = LLNotification::getSelectedOption(notification, response);

	LLStringUtil::trim(newSetName);

	LLUIString new_set_name = newSetName;

	if (newSetName.empty())
	{
		return false;
	}
	else if (
		!validAOName(new_set_name.getWString()) ||		// only allow ASCII
		newSetName.find_first_of(":|") != std::string::npos)							// don't allow : or |
	{
		LLSD args;
		args["AO_SET_NAME"] = newSetName;
		gNotifications.add("NewAOCantContainNonASCII", args);
		return false;
	}

	if (option == 0)
	{
		if (AOEngine::instance().addSet(newSetName).notNull())
		{
			reloading(true);
			return true;
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

	if (option ==0 )
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
		AOEngine::instance().setDefaultSet(mSelectedSet);
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

	BOOL resortEnable = FALSE;
	BOOL trashEnable = FALSE;

	// Linden Lab bug: scroll lists still select the first item when you click on them, even when they are disabled.
	// The control does not memorize it's enabled/disabled state, so mAnimationList->mEnabled() doesn't seem to work.
	// So we need to safeguard against it.
	if (!mCanDragAndDrop)
	{
		mAnimationList->deselectAllItems();
		LL_DEBUGS("AOEngine") << "Selection count now: " << list.size() << llendl;
	}
	else if (list.size() > 0)
	{
		if (list.size() == 1)
		{
			resortEnable = TRUE;
		}
		trashEnable = TRUE;
	}

	mMoveDownButton->setEnabled(resortEnable);
	mMoveUpButton->setEnabled(resortEnable);
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

	S32 currentIndex = mAnimationList->getFirstSelectedIndex();
	if (currentIndex == -1)
	{
		return;
	}

	if (AOEngine::instance().swapWithPrevious(mSelectedState, currentIndex))
	{
		mAnimationList->swapWithPrevious(currentIndex);
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

	S32 currentIndex = mAnimationList->getFirstSelectedIndex();
	if (currentIndex >= (mAnimationList->getItemCount() - 1))
	{
		return;
	}

	if (AOEngine::instance().swapWithNext(mSelectedState, currentIndex))
	{
		mAnimationList->swapWithNext(currentIndex);
	}
}

void FloaterAO::onClickTrash()
{
	if (!mSelectedState)
	{
		return;
	}

	std::vector<LLScrollListItem*> list = mAnimationList->getAllSelected();
	if (list.empty())
	{
		return;
	}

	for (S32 index = list.size() - 1; index != -1; --index)
	{
		AOEngine::instance().removeAnimation(mSelectedSet, mSelectedState, mAnimationList->getItemIndex(list[index]));
	}

	mAnimationList->deleteSelectedItems();
	mCurrentBoldItem = nullptr;
}

void FloaterAO::updateCycleParameters()
{
	BOOL enabled = mCycleCheckBox->getValue().asBoolean();
	mRandomizeCheckBox->setEnabled(enabled);
	mCycleTimeTextLabel->setEnabled(enabled);
	mCycleTimeSpinner->setEnabled(enabled);
}

void FloaterAO::onCheckCycle()
{
	if (mSelectedState)
	{
		bool cycle = mCycleCheckBox->getValue().asBoolean();
		AOEngine::instance().setCycle(mSelectedState, cycle);
		updateCycleParameters();
	}
}

void FloaterAO::onCheckRandomize()
{
	if (mSelectedState)
	{
		AOEngine::instance().setRandomize(mSelectedState, mRandomizeCheckBox->getValue().asBoolean());
	}
}

void FloaterAO::onChangeCycleTime()
{
	if (mSelectedState)
	{
		AOEngine::instance().setCycleTime(mSelectedState, (F32)mCycleTimeSpinner->getValue().asReal());
	}
}

void FloaterAO::onClickPrevious()
{
	AOEngine::instance().cycle(AOEngine::CyclePrevious);
}

void FloaterAO::onClickNext()
{
	AOEngine::instance().cycle(AOEngine::CycleNext);
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

	mSmallInterfacePanel->setVisible(FALSE);
	mMainInterfacePanel->setVisible(TRUE);
	setCanResize(TRUE);

	gSavedPerAccountSettings.setBool("UseFullAOInterface", TRUE);

	reshape(getRect().getWidth(), fullSize.getHeight());
}

void FloaterAO::onClickLess()
{
	LLRect fullSize = getRect();
	LLRect smallSize = mSmallInterfacePanel->getRect();
	smallSize.setLeftTopAndSize(0, 0, smallSize.getWidth(), smallSize.getHeight() + LLFLOATER_HEADER_SIZE);

	gSavedPerAccountSettings.setRect("floater_rect_animation_overrider_full", fullSize);

	mMore = false;

	mSmallInterfacePanel->setVisible(TRUE);
	mMainInterfacePanel->setVisible(FALSE);
	setCanResize(FALSE);

	gSavedPerAccountSettings.setBool("UseFullAOInterface", FALSE);

	reshape(getRect().getWidth(), smallSize.getHeight());

	// save current size and position
	gSavedPerAccountSettings.setRect("floater_rect_animation_overrider_full", fullSize);
}

void FloaterAO::onAnimationChanged(const LLUUID& animation)
{
	LL_DEBUGS("AOEngine") << "Received animation change to " << animation << llendl;

	if (mCurrentBoldItem)
	{
		LLScrollListText* column = (LLScrollListText*)mCurrentBoldItem->getColumn(1);
		column->setFontStyle(LLFontGL::NORMAL);

		mCurrentBoldItem = nullptr;
	}

	if (animation.isNull())
	{
		return;
	}

	// why do we have no LLScrollListCtrl::getItemByUserdata() ? -Zi
	std::vector<LLScrollListItem*> item_list = mAnimationList->getAllData();
	std::vector<LLScrollListItem*>::const_iterator iter;
	for (iter = item_list.begin(); iter != item_list.end(); ++iter)
	{
		LLScrollListItem* item = *iter;
		LLUUID* id = (LLUUID*)item->getUserdata();

		if (id == &animation)
		{
			mCurrentBoldItem = item;

			LLScrollListText* column = (LLScrollListText*)mCurrentBoldItem->getColumn(1);
			column->setFontStyle(LLFontGL::BOLD);

			return;
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
		return TRUE;
	}

	LLInventoryItem* item = (LLInventoryItem*)data;

	if (type == DAD_NOTECARD)
	{
		if (mImportRunning)
		{
			*accept = ACCEPT_NO;
			return TRUE;
		}
		*accept = ACCEPT_YES_SINGLE;
		if (item && drop)
		{
			if (AOEngine::instance().importNotecard(item))
			{
				reloading(TRUE);
				mReloadButton->setEnabled(FALSE);
				mImportRunning = TRUE;
			}
		}
	}
	else if (type == DAD_ANIMATION)
	{
		if (!drop && (!mSelectedSet || !mSelectedState || !mCanDragAndDrop))
		{
			*accept = ACCEPT_NO;
			return TRUE;
		}
		*accept = ACCEPT_YES_MULTI;
		if (item && drop)
		{
			if (AOEngine::instance().addAnimation(mSelectedSet, mSelectedState, item))
			{
				addAnimation(item->getName());

				// TODO: this would be the right thing to do, but it blocks multi drop
				// before final release this must be resolved
				reloading(true);
			}
		}
	}
	else
	{
		*accept = ACCEPT_NO;
	}

	return TRUE;
}
