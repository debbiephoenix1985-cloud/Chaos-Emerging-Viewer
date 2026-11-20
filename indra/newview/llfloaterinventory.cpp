/**
 * @filell llfloaterinventory.cpp
 * @brief Implementation of the inventory floater and associated stuff.
 *
 * $LicenseInfo:firstyear=2001&license=viewergpl$
 *
 * Copyright (c) 2001-2009, Linden Research, Inc.
 *
 * Second Life Viewer Source Code
 * The source code in this file ("Source Code") is provided by Linden Lab
 * to you under the terms of the GNU General Public License, version 2.0
 * ("GPL"), unless you have obtained a separate licensing agreement
 * ("Other License"), formally executed by you and Linden Lab.  Terms of
 * the GPL can be found in doc/GPL-license.txt in this distribution, or
 * online at http://secondlifegrid.net/programs/open_source/licensing/gplv2
 *
 * There are special exceptions to the terms and conditions of the GPL as
 * it is applied to this Source Code. View the full text of the exception
 * in the file doc/FLOSS-exception.txt in this software distribution.
 *
 * By copying, modifying or distributing this software, you acknowledge
 * that you have read and understood your obligations described above,
 * and agree to abide by those obligations.
 *
 * ALL LINDEN LAB SOURCE CODE IS PROVIDED "AS IS." LINDEN LAB MAKES NO
 * WARRANTIES, EXPRESS, IMPLIED OR OTHERWISE, REGARDING ITS ACCURACY,
 * COMPLETENESS OR PERFORMANCE.
 * $/LicenseInfo$
 */

#include "llviewerprecompiledheaders.h"

#include "llfloaterinventory.h"

#include "llcallbacklist.h"
#include "llcheckboxctrl.h"
#include "llbutton.h"
#include "llmenugl.h"
#include "llnotifications.h"
#include "lldir.h"
#include "llfasttimer.h"
#include "lllocale.h"
#include "llscrollcontainer.h"
#include "llsdserialize.h"
#include "llspinctrl.h"
#include "lltabcontainer.h"
#include "lluictrlfactory.h"
#include "llwindow.h"
#include "llmessage.h"

#include "llagent.h"
#include "llappviewer.h"
#include "llavatartracker.h"
#include "llfirstuse.h"
#include "llfloateravatarinfo.h"
#include "llfloaterchat.h"
#include "llfloatercustomize.h"
#include "hbfloaterthumbnail.h"
#include "llgesturemgr.h"
#include "llinventoryactions.h"
#include "llinventorybridge.h"
#include "llinventorymodelfetch.h"
#include "llmarketplacefunctions.h"
#include "llpreviewanim.h"
#include "llpreviewgesture.h"
#include "llpreviewlandmark.h"
#include "llpreviewnotecard.h"
#include "llpreviewscript.h"
#include "llpreviewsound.h"
#include "llpreviewtexture.h"
//MK
#include "mkrlinterface.h"
//mk
#include "llselectmgr.h"
#include "llstartup.h"
#include "lltooldraganddrop.h"
#include "llviewercontrol.h"
#include "llviewerinventory.h"
#include "llviewermenu.h"
#include "llviewerobjectlist.h"
#include "llviewerregion.h"
#include "llviewertexturelist.h"
#include "llwearablelist.h"

std::vector<LLFloaterInventory*> LLFloaterInventory::sActiveViews;

constexpr S32 INV_MIN_WIDTH = 240;
constexpr S32 INV_MIN_HEIGHT = 150;
constexpr S32 INV_FINDER_WIDTH = 300;
constexpr S32 INV_FINDER_HEIGHT = 430;

///----------------------------------------------------------------------------
/// LLFloaterInventoryFilters
///----------------------------------------------------------------------------

LLFloaterInventoryFilters::LLFloaterInventoryFilters(const std::string& name,
													 const LLRect& rect,
													 LLFloaterInventory* inv)
:	LLFloater(name, rect, "Filters", RESIZE_NO, INV_FINDER_WIDTH,
			  INV_FINDER_HEIGHT, DRAG_ON_TOP, MINIMIZE_NO, CLOSE_YES),
	mInventoryView(inv),
	mFilter(inv->mActivePanel->getFilter()),
	mHasMaterial(gAgent.hasRegionCapability("UpdateMaterialAgentInventory"))
{
	LLUICtrlFactory::getInstance()->buildFloater(this,
												 "floater_inventory_filters.xml");
}

//virtual
bool LLFloaterInventoryFilters::postBuild()
{
	childSetAction("All", selectAllTypes, this);
	childSetAction("None", selectNoTypes, this);
	childSetAction("Reset", onResetFilters, this);
	childSetAction("Close", onCloseBtn, this);

	mSpinSinceHours = getChild<LLSpinCtrl>("spin_hours_ago");
	mSpinSinceHours->setCommitCallback(onTimeAgo);
	mSpinSinceHours->setCallbackUserData(this);

	mSpinSinceDays = getChild<LLSpinCtrl>("spin_days_ago");
	mSpinSinceDays->setCommitCallback(onTimeAgo);
	mSpinSinceDays->setCallbackUserData(this);

	mCheckSinceLogoff = getChild<LLCheckBoxCtrl>("check_since_logoff");
	mCheckShowEmpty = getChild<LLCheckBoxCtrl>("check_show_empty");

	mCheckAnimation = getChild<LLCheckBoxCtrl>("check_animation");
	mCheckCallingcard = getChild<LLCheckBoxCtrl>("check_callingcard");
	mCheckClothing = getChild<LLCheckBoxCtrl>("check_clothing");
	mCheckGesture = getChild<LLCheckBoxCtrl>("check_gesture");
	mCheckLandmark = getChild<LLCheckBoxCtrl>("check_landmark");
	mCheckMaterial = getChild<LLCheckBoxCtrl>("check_material");
	if (!mHasMaterial)
	{
		mCheckMaterial->set(false);
		mCheckMaterial->setEnabled(false);
		std::string tooltip = getString("not_supported");
		childSetToolTip("check_material", tooltip);
		childSetToolTip("icon_material", tooltip);
	}
	mCheckNotecard = getChild<LLCheckBoxCtrl>("check_notecard");
	mCheckObject = getChild<LLCheckBoxCtrl>("check_object");
	mCheckScript = getChild<LLCheckBoxCtrl>("check_script");
	mCheckSnapshot = getChild<LLCheckBoxCtrl>("check_snapshot");
	mCheckSound = getChild<LLCheckBoxCtrl>("check_sound");
	mCheckTexture = getChild<LLCheckBoxCtrl>("check_texture");
#if LL_MESH_ASSET_SUPPORT
	mCheckMesh = getChild<LLCheckBoxCtrl>("check_mesh");
	mCheckMesh->setEnabled(true);
	childEnable("icon_mesh");
#else
	std::string tooltip = getString("mesh_deprecated");
	childSetToolTip("check_mesh", tooltip);
	childSetToolTip("icon_mesh", tooltip);
#endif
	mCheckSettings = getChild<LLCheckBoxCtrl>("check_settings");

	updateElementsFromFilter();

	return true;
}

void LLFloaterInventoryFilters::onResetFilters(void* datap)
{
	LLFloaterInventoryFilters* self = (LLFloaterInventoryFilters*)datap;
	if (self)
	{
		LLInventoryPanel* panelp = self->mInventoryView->mActivePanel;
		if (panelp)
		{
			panelp->getFilter()->resetDefault();
			self->updateElementsFromFilter();
			self->mInventoryView->setFilterTextFromFilter();
		}
	}
}

void LLFloaterInventoryFilters::onTimeAgo(LLUICtrl*, void* datap)
{
	LLFloaterInventoryFilters* self = (LLFloaterInventoryFilters*)datap;
	if (self)
	{
		self->mCheckSinceLogoff->set(!self->mSpinSinceDays->get() &&
									 !self->mSpinSinceHours->get());
	}
}

void LLFloaterInventoryFilters::changeFilter(LLInventoryFilter* filterp)
{
	mFilter = filterp;
	updateElementsFromFilter();
}

void LLFloaterInventoryFilters::updateElementsFromFilter()
{
	if (!mFilter)
	{
		return;
	}

	U32 filter = mFilter->getFilterTypes();
	mCheckAnimation->set(filter & 0x1 << LLInventoryType::IT_ANIMATION);
	mCheckCallingcard->set(filter & 0x1 << LLInventoryType::IT_CALLINGCARD);
	mCheckClothing->set(filter & 0x1 << LLInventoryType::IT_WEARABLE);
	mCheckGesture->set(filter & 0x1 << LLInventoryType::IT_GESTURE);
	mCheckLandmark->set(filter & 0x1 << LLInventoryType::IT_LANDMARK);
	mCheckMaterial->set(mHasMaterial &&
						(filter & 0x1 << LLInventoryType::IT_MATERIAL));
	mCheckNotecard->set(filter & 0x1 << LLInventoryType::IT_NOTECARD);
	mCheckObject->set(filter & 0x1 << LLInventoryType::IT_OBJECT);
	mCheckScript->set(filter & 0x1 << LLInventoryType::IT_LSL);
	mCheckSound->set(filter & 0x1 << LLInventoryType::IT_SOUND);
	mCheckTexture->set(filter & 0x1 << LLInventoryType::IT_TEXTURE);
	mCheckSnapshot->set(filter & 0x1 << LLInventoryType::IT_SNAPSHOT);
#if LL_MESH_ASSET_SUPPORT
	mCheckMesh->set(filter & 0x1 << LLInventoryType::IT_MESH);
#endif
	mCheckSettings->set(filter & 0x1 << LLInventoryType::IT_SETTINGS);

	mCheckShowEmpty->set(mFilter->getShowFolderState() ==
						 LLInventoryFilter::SHOW_ALL_FOLDERS);

	mCheckSinceLogoff->set(mFilter->isSinceLogoff());

	U32 hours = mFilter->getHoursAgo();
	mSpinSinceHours->set((F32)(hours % 24));
	mSpinSinceDays->set((F32)(hours / 24));
}

void LLFloaterInventoryFilters::draw()
{
//MK
	// Fast enough that it can be kept here
	if (gRLenabled && gRLInterface.mContainsShowinv)
	{
		close();
		return;
	}
//mk

	U32 filter = 0xffffffff;
	bool filtered_by_all_types = true;

	if (!mCheckAnimation->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_ANIMATION);
		filtered_by_all_types = false;
	}

	if (!mCheckCallingcard->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_CALLINGCARD);
		filtered_by_all_types = false;
	}

	if (!mCheckClothing->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_WEARABLE);
		filtered_by_all_types = false;
	}

	if (!mCheckGesture->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_GESTURE);
		filtered_by_all_types = false;
	}

	if (!mCheckLandmark->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_LANDMARK);
		filtered_by_all_types = false;
	}

	if (mHasMaterial && !mCheckMaterial->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_MATERIAL);
		filtered_by_all_types = false;
	}

	if (!mCheckNotecard->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_NOTECARD);
		filtered_by_all_types = false;
	}

	if (!mCheckObject->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_OBJECT);
		filter &= ~(0x1 << LLInventoryType::IT_ATTACHMENT);
		filtered_by_all_types = false;
	}

	if (!mCheckScript->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_LSL);
		filtered_by_all_types = false;
	}

	if (!mCheckSound->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_SOUND);
		filtered_by_all_types = false;
	}

	if (!mCheckTexture->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_TEXTURE);
		filtered_by_all_types = false;
	}

	if (!mCheckSnapshot->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_SNAPSHOT);
		filtered_by_all_types = false;
	}

#if LL_MESH_ASSET_SUPPORT
	if (!mCheckMesh->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_MESH);
		filtered_by_all_types = false;
	}
#endif

	if (!mCheckSettings->get())
	{
		filter &= ~(0x1 << LLInventoryType::IT_SETTINGS);
		filtered_by_all_types = false;
	}

	if (!filtered_by_all_types)
	{
		// Do not include folders in filter, unless everything is selected
		filter &= ~(0x1 << LLInventoryType::IT_CATEGORY);
	}

	LLInventoryPanel* panelp = mInventoryView->mActivePanel;
	if (panelp)
	{
		// Update the panel, which will update the filter
		LLInventoryFilter::EFolderShow show =
			mCheckShowEmpty->get() ? LLInventoryFilter::SHOW_ALL_FOLDERS
								   : LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS;
		panelp->setShowFolderState(show);
		panelp->setFilterTypes(filter);

		bool since_logoff = mCheckSinceLogoff->get();
		if (since_logoff)
		{
			mSpinSinceDays->set(0);
			mSpinSinceHours->set(0);
		}
		U32 days = (U32)mSpinSinceDays->get();
		U32 hours = (U32)mSpinSinceHours->get();
		if (hours > 24)
		{
			days += hours / 24;
			hours = (U32)hours % 24;
			mSpinSinceDays->set((F32)days);
			mSpinSinceHours->set((F32)hours);
		}
		hours += days * 24;
		panelp->setHoursAgo(hours);
		panelp->setSinceLogoff(since_logoff);
	}

	mInventoryView->setFilterTextFromFilter();

	LLFloater::draw();
}

void LLFloaterInventoryFilters::onClose(bool app_quitting)
{
	if (mInventoryView)
	{
		mInventoryView->getControl("Inventory.ShowFilters")->setValue(false);
	}
	destroy();
}

void LLFloaterInventoryFilters::onCloseBtn(void* datap)
{
	LLFloaterInventoryFilters* self = (LLFloaterInventoryFilters*)datap;
	if (self)
	{
		self->close();
	}
}

//static
void LLFloaterInventoryFilters::selectAllTypes(void* datap)
{
	LLFloaterInventoryFilters* self = (LLFloaterInventoryFilters*)datap;
	if (!self) return;

	self->mCheckAnimation->set(true);
	self->mCheckCallingcard->set(true);
	self->mCheckClothing->set(true);
	self->mCheckGesture->set(true);
	self->mCheckLandmark->set(true);
	self->mCheckMaterial->set(self->mHasMaterial);
	self->mCheckNotecard->set(true);
	self->mCheckObject->set(true);
	self->mCheckScript->set(true);
	self->mCheckSound->set(true);
	self->mCheckTexture->set(true);
	self->mCheckSnapshot->set(true);
#if LL_MESH_ASSET_SUPPORT
	self->mCheckMesh->set(true);
#endif
	self->mCheckSettings->set(true);
}

//static
void LLFloaterInventoryFilters::selectNoTypes(void* datap)
{
	LLFloaterInventoryFilters* self = (LLFloaterInventoryFilters*)datap;
	if (!self) return;

	self->mCheckAnimation->set(false);
	self->mCheckCallingcard->set(false);
	self->mCheckClothing->set(false);
	self->mCheckGesture->set(false);
	self->mCheckLandmark->set(false);
	self->mCheckMaterial->set(false);
	self->mCheckNotecard->set(false);
	self->mCheckObject->set(false);
	self->mCheckScript->set(false);
	self->mCheckSound->set(false);
	self->mCheckTexture->set(false);
	self->mCheckSnapshot->set(false);
#if LL_MESH_ASSET_SUPPORT
	self->mCheckMesh->set(false);
#endif
	self->mCheckSettings->set(false);
}

///----------------------------------------------------------------------------
/// Helper classes for LLFloaterInventory
///----------------------------------------------------------------------------

void LLSaveFolderState::setApply(bool apply)
{
	mApply = apply;
	// Before generating new list of open folders, clear the old one
	if (!apply)
	{
		clearOpenFolders();
	}
}

void LLSaveFolderState::doFolder(LLFolderViewFolder* folder)
{
	if (!folder) return;	// Paranoia

	if (mApply)
	{
		// We are applying the open state
		LLInvFVBridge* bridge = (LLInvFVBridge*)folder->getListener();
		if (!bridge) return;

		const LLUUID& id = bridge->getUUID();
		if (mOpenFolders.find(id) != mOpenFolders.end())
		{
			folder->setOpen(true);
		}
		else if (!folder->isSelected())
		{
			// Keep selected filter in its current state, this is less jarring
			// to user
			folder->setOpen(false);
		}
	}
	else if (folder->isOpen())
	{
		// We are recording state at this point
		LLInvFVBridge* bridge = (LLInvFVBridge*)folder->getListener();
		if (bridge)
		{
			mOpenFolders.emplace(bridge->getUUID());
		}
	}
}

void LLOpenFilteredFolders::doItem(LLFolderViewItem* item)
{
	if (item && item->getFiltered())
	{
		LLFolderViewFolder* parentp = item->getParentFolder();
		if (parentp)
		{
			parentp->setOpenArrangeRecursively(true,
											   LLFolderViewFolder::RECURSE_UP);
		}
	}
}

void LLOpenFilteredFolders::doFolder(LLFolderViewFolder* folder)
{
	if (!folder) return;	// Paranoia

	LLFolderViewFolder* parentp = folder->getParentFolder();
	if (parentp && folder->getFiltered())
	{
		parentp->setOpenArrangeRecursively(true,
										   LLFolderViewFolder::RECURSE_UP);
	}
	// If this folder did not pass the filter, and none of its descendants did
	else if (!folder->getFiltered() && !folder->hasFilteredDescendants())
	{
		folder->setOpenArrangeRecursively(false,
										  LLFolderViewFolder::RECURSE_NO);
	}
}

void LLOpenFoldersWithSelection::doItem(LLFolderViewItem* item)
{
	if (item && item->getParentFolder() && item->isSelected())
	{
		LLFolderViewFolder* parentp = item->getParentFolder();
		if (parentp)
		{
			parentp->setOpenArrangeRecursively(true,
											   LLFolderViewFolder::RECURSE_UP);
		}
	}
}

void LLOpenFoldersWithSelection::doFolder(LLFolderViewFolder* folder)
{
	if (folder && folder->getParentFolder() && folder->isSelected())
	{
		LLFolderViewFolder* parentp = folder->getParentFolder();
		if (parentp)
		{
			parentp->setOpenArrangeRecursively(true,
											   LLFolderViewFolder::RECURSE_UP);
		}
	}
}

///----------------------------------------------------------------------------
/// LLFloaterInventory
///----------------------------------------------------------------------------

// Default constructor
LLFloaterInventory::LLFloaterInventory(const std::string& name,
									   const std::string& rect,
									   LLInventoryModel* modelp)
:	LLFloater(name, rect, "Inventory", RESIZE_YES, INV_MIN_WIDTH,
			  INV_MIN_HEIGHT, DRAG_ON_TOP, MINIMIZE_NO, CLOSE_YES),
	mActivePanel(NULL)
	// LLHandle<LLFloater> mInvFiltersHandle takes care of its own
	// initialization
{
	init(modelp);
}

LLFloaterInventory::LLFloaterInventory(const std::string& name,
									   const LLRect& rect,
									   LLInventoryModel* modelp)
:	LLFloater(name, rect, "Inventory", RESIZE_YES, INV_MIN_WIDTH,
			  INV_MIN_HEIGHT, DRAG_ON_TOP, MINIMIZE_NO, CLOSE_YES),
	mActivePanel(NULL)
	// LLHandle<LLFloater> mInvFiltersHandle takes care of its own
	// initialization
{
	init(modelp);
	setRect(rect);	// Override XML
}

void LLFloaterInventory::init(LLInventoryModel* modelp)
{
	mLastCount = 0;

	// Callbacks
	init_inventory_actions(this);

	// Controls
	addBoolControl("Inventory.ShowFilters", false);
	addBoolControl("Inventory.SortByName", false);
	addBoolControl("Inventory.SortByDate", true);
	addBoolControl("Inventory.FoldersAlwaysByName", true);
	addBoolControl("Inventory.SystemFoldersToTop", true);
	updateSortControls();

	addBoolControl("Inventory.SearchName", true);
	addBoolControl("Inventory.SearchDesc", false);
	addBoolControl("Inventory.SearchCreator", false);
	addBoolControl("Inventory.SearchShowLinks", false);
	updateSearchControls();

	LLUICtrlFactory::getInstance()->buildFloater(this,
												 "floater_inventory.xml");

	// Now load the stored settings from disk, if available.
	std::ostringstream filter_sream;
	filter_sream << gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT, "filters.xml");
	llifstream file(filter_sream.str().c_str());
	if (file.is_open())
	{
		llinfos << "Reading filters settings from " << filter_sream.str()
				<< llendl;

		LLSD saved_filter_state;
		LLSDSerialize::fromXML(saved_filter_state, file);
		file.close();

		// Load the persistent "Recent Items" settings. Note that the "All
		// Items" and "Worn Items" settings do not persist per-account.
		if (mRecentPanel &&
			saved_filter_state.has(mRecentPanel->getFilter()->getName()))
		{
			LLSD recent_items =
				saved_filter_state.get(mRecentPanel->getFilter()->getName());
			mRecentPanel->getFilter()->fromLLSD(recent_items);
		}
	}

	sActiveViews.push_back(this);

	gInventory.addObserver(this);

	// *HACK: make sure everything is fetched (especially thumbnails for
	// folders parented to the root). HB
	LLInventoryModelFetch::forceFetchFolder(modelp->getRootFolderID());
}

static void collapseInventoryFolders(void* userdata)
{
	LLFloaterInventory* inventoryp = (LLFloaterInventory*)userdata;
	LLInventoryPanel* panelp = inventoryp ? inventoryp->getActivePanel() : NULL;
	if (panelp)
	{
		panelp->closeAllFolders();
	}
}

static void expandInventoryFolders(void* userdata)
{
	LLFloaterInventory* inventoryp = (LLFloaterInventory*)userdata;
	LLInventoryPanel* panelp = inventoryp ? inventoryp->getActivePanel() : NULL;
	LLFolderView* rootp = panelp ? panelp->getRootFolder() : NULL;
	if (rootp)
	{
		rootp->setOpenArrangeRecursively(true, LLFolderViewFolder::RECURSE_DOWN);
	}
}

// Native panel creation for Firestorm 7.2.5 custom filter tabs (FIRE-35598).
// This is the panel integration layer; tab management and persistence follow.
LLInventoryPanel* LLFloaterInventory::createCustomFilterTab(const std::string& label)
{
	if (!mFilterTabs || !mEverythingPanel || label.empty() ||
		mCustomFilterPanels.size() >= 64)
	{
		return NULL;
	}

	// Use a unique internal identifier, including after deleting a middle tab.
	U32 serial = 1;
	std::string panel_name;
	for (;;)
	{
		panel_name = "ChaosCustomInventoryFilter" + std::to_string(serial++);
		bool used = false;
		for (LLInventoryPanel* existing : mCustomFilterPanels)
		{
			if (existing && existing->getName() == panel_name)
			{
				used = true;
				break;
			}
		}
		if (!used) break;
	}
	LLInventoryPanel* panelp = new LLInventoryPanel(
		panel_name, "", mEverythingPanel->getRect(),
		mEverythingPanel->getModel(), true, false, false);
	panelp->setFollowsAll();
	if (!panelp->postBuild())
	{
		delete panelp;
		return NULL;
	}

	panelp->setSortOrder(gSavedSettings.getU32("InventorySortOrder"));
	panelp->getFilter()->markDefault();
	panelp->setSelectCallback(onSelectionChange, panelp);
	// Insert before both the hidden-tab selector and permanent "+" tab.
	const bool hidden_selector_attached = mShowHiddenCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mShowHiddenCustomTabPanel) >= 0;
	if (mAddCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mAddCustomTabPanel) >= 0)
		mFilterTabs->removeTabPanel(mAddCustomTabPanel);
	if (hidden_selector_attached)
		mFilterTabs->removeTabPanel(mShowHiddenCustomTabPanel);
	mFilterTabs->addTabPanel(panelp, label, true, onFilterSelected, this);
	mCustomFilterPanels.push_back(panelp);
	mCustomTabLabels[panelp] = label;
	mActivePanel = panelp;
	if (hidden_selector_attached)
	{
	    mFilterTabs->addTabPanel(mShowHiddenCustomTabPanel, "▼", false,
	    NULL, this);
	    mFilterTabs->setTabPrecommitChangeCallback(mShowHiddenCustomTabPanel, onShowHiddenCustomTabPrecommit);
	    mFilterTabs->setTabButtonTooltip(mShowHiddenCustomTabPanel, "Restore a hidden custom inventory tab");
	}
	if (mAddCustomTabPanel)
	{
	    mFilterTabs->addTabPanel(mAddCustomTabPanel, "+", false,
	    NULL, this);
	    mFilterTabs->setTabPrecommitChangeCallback(mAddCustomTabPanel, onAddCustomTabPrecommit);
	    mFilterTabs->setTabButtonTooltip(mAddCustomTabPanel, "Add custom inventory filter tab");
	}
	return panelp;
}

void LLFloaterInventory::addCustomFilterTab()
{
	const std::string label = "Custom " +
		std::to_string(mCustomFilterPanels.size() + 1);
	if (createCustomFilterTab(label))
	{
		saveCustomFilterTabs();
	}
}

void LLFloaterInventory::cloneCustomFilterTab()
{
	LLInventoryPanel* source = mActivePanel;
	if (!source || mCustomFilterPanels.size() >= 64)
	{
		return;
	}
	const S32 idx = mFilterTabs->getIndexForPanel(source);
	const std::string label = idx >= 0 ? mFilterTabs->getPanelTitle(idx) + " copy" : "Custom copy";
	LLSD state;
	source->getFilter()->toLLSD(state);
	LLInventoryPanel* clone = createCustomFilterTab(label);
	if (clone)
	{
		clone->getFilter()->fromLLSD(state);
		clone->getFilter()->setModified();
		saveCustomFilterTabs();
	}
}

bool LLFloaterInventory::isCustomFilterTab(LLInventoryPanel* panel) const
{
	return panel && std::find(mCustomFilterPanels.begin(),
		mCustomFilterPanels.end(), panel) != mCustomFilterPanels.end();
}

void LLFloaterInventory::renameCustomFilterTab()
{
	LLInventoryPanel* panel = mActivePanel;
	if (std::find(mCustomFilterPanels.begin(), mCustomFilterPanels.end(), panel) ==
		mCustomFilterPanels.end()) return;
	const S32 index = mFilterTabs->getIndexForPanel(panel);
	if (index < 0) return;
	LLSD args;
	args["NAME"] = mFilterTabs->getPanelTitle(index);
	LLSD payload;
	payload["panel_name"] = panel->getName();
	LLHandle<LLView> handle = getHandle();
	gNotifications.add("ChaosInventoryCustomTabRename", args, payload,
		[handle](const LLSD& notification, const LLSD& response) -> bool
		{
			if (LLNotification::getSelectedOption(notification, response) != 0)
				return false;
			LLFloaterInventory* self = dynamic_cast<LLFloaterInventory*>(handle.get());
			if (!self || !self->mFilterTabs) return false;
			std::string name = response["message"].asString();
			LLStringUtil::trim(name);
			if (name.empty() || name.size() > 128) return false;
			const std::string panel_name = notification["payload"]["panel_name"].asString();
			for (LLInventoryPanel* candidate : self->mCustomFilterPanels)
			{
				if (candidate && candidate->getName() == panel_name)
				{
					const S32 idx = self->mFilterTabs->getIndexForPanel(candidate);
					if (idx >= 0)
					{
						self->mFilterTabs->setPanelTitle(idx, name);
						self->saveCustomFilterTabs();
					}
					break;
				}
			}
			return false;
		});
}

void LLFloaterInventory::removeCustomFilterTabByName(const std::string& panel_name)
{
	for (LLInventoryPanel* panel : mCustomFilterPanels)
	{
		if (panel && panel->getName() == panel_name &&
			!mHiddenCustomFilterPanels.count(panel))
		{
			mFilterTabs->selectTabPanel(panel);
			mActivePanel = panel;
			closeCustomFilterTab();
			return;
		}
	}
}

void LLFloaterInventory::requestCloseCustomFilterTab(LLInventoryPanel* panel)
{
	if (!isCustomFilterTab(panel) || !mFilterTabs ||
		mHiddenCustomFilterPanels.count(panel)) return;
	const S32 index = mFilterTabs->getIndexForPanel(panel);
	if (index < 0) return;
	LLSD args;
	args["NAME"] = mFilterTabs->getPanelTitle(index);
	LLSD payload;
	payload["panel_name"] = panel->getName();
	LLHandle<LLView> handle = getHandle();
	gNotifications.add("ChaosInventoryCustomTabClose", args, payload,
		[handle](const LLSD& notification, const LLSD& response) -> bool
		{
			if (LLNotification::getSelectedOption(notification, response) == 0)
			{
				LLFloaterInventory* inv =
					dynamic_cast<LLFloaterInventory*>(handle.get());
				if (inv) inv->removeCustomFilterTabByName(
					notification["payload"]["panel_name"].asString());
			}
			return false;
		});
}

void LLFloaterInventory::closeCustomFilterTab()
{
	std::vector<LLInventoryPanel*>::iterator it =
		std::find(mCustomFilterPanels.begin(), mCustomFilterPanels.end(), mActivePanel);
	if (it == mCustomFilterPanels.end() ||
		mHiddenCustomFilterPanels.count(mActivePanel))
	{
		return; // Never close built-in tabs or detached hidden tabs.
	}
	LLInventoryPanel* closing = *it;
	mFilterTabs->selectTabPanel(mEverythingPanel);
	mActivePanel = mEverythingPanel;
	mFilterTabs->removeTabPanel(closing);
	mCustomFilterPanels.erase(it);
	mCustomTabLabels.erase(closing);
	if (mContextCustomFilterPanel == closing) mContextCustomFilterPanel = nullptr;
	delete closing; // Legacy tab container detaches without deleting the panel.
	saveCustomFilterTabs();
}

void LLFloaterInventory::hideCustomFilterTab(LLInventoryPanel* panel)
{
	if (!isCustomFilterTab(panel) || mHiddenCustomFilterPanels.count(panel) ||
		!mFilterTabs) return;
	const S32 index = mFilterTabs->getIndexForPanel(panel);
	if (index < 0) return;
	mCustomTabLabels[panel] = mFilterTabs->getPanelTitle(index);
	LLPanel* prior = mFilterTabs->getCurrentPanel();
	// Remove from the legacy tab strip only while a stable built-in tab is selected.
	mFilterTabs->selectTabPanel(mEverythingPanel);
	mActivePanel = mEverythingPanel;
	mFilterTabs->removeTabPanel(panel); // Detach, retain filter and contents.
	mHiddenCustomFilterPanels.insert(panel);
	if (mContextCustomFilterPanel == panel) mContextCustomFilterPanel = nullptr;
	updateShowHiddenCustomTab();
	if (prior != panel && prior != mEverythingPanel &&
		mFilterTabs->getIndexForPanel(prior) >= 0)
		mFilterTabs->selectTabPanel(prior);
	saveCustomFilterTabs();
}

void LLFloaterInventory::showCustomFilterTab(LLInventoryPanel* panel)
{
	if (!isCustomFilterTab(panel) || !mHiddenCustomFilterPanels.count(panel))
		return;
	if (mAddCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mAddCustomTabPanel) >= 0)
		mFilterTabs->removeTabPanel(mAddCustomTabPanel);
	if (mShowHiddenCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mShowHiddenCustomTabPanel) >= 0)
		mFilterTabs->removeTabPanel(mShowHiddenCustomTabPanel);
	mHiddenCustomFilterPanels.erase(panel);
	const auto label_it = mCustomTabLabels.find(panel);
	const std::string label = label_it == mCustomTabLabels.end() ?
		"Custom" : label_it->second;
	mFilterTabs->addTabPanel(panel, label, true, onFilterSelected, this);
	mActivePanel = panel;
	if (!mHiddenCustomFilterPanels.empty())
	{
	    mFilterTabs->addTabPanel(mShowHiddenCustomTabPanel, "▼", false,
	    NULL, this);
	    mFilterTabs->setTabPrecommitChangeCallback(mShowHiddenCustomTabPanel, onShowHiddenCustomTabPrecommit);
	    mFilterTabs->setTabButtonTooltip(mShowHiddenCustomTabPanel, "Restore a hidden custom inventory tab");
	}
	if (mAddCustomTabPanel)
	{
	    mFilterTabs->addTabPanel(mAddCustomTabPanel, "+", false,
	    NULL, this);
	    mFilterTabs->setTabPrecommitChangeCallback(mAddCustomTabPanel, onAddCustomTabPrecommit);
	    mFilterTabs->setTabButtonTooltip(mAddCustomTabPanel, "Add custom inventory filter tab");
	}
	saveCustomFilterTabs();
}

void LLFloaterInventory::updateShowHiddenCustomTab()
{
	if (!mShowHiddenCustomTabPanel || !mFilterTabs) return;
	const bool attached =
		mFilterTabs->getIndexForPanel(mShowHiddenCustomTabPanel) >= 0;
	if (mHiddenCustomFilterPanels.empty())
	{
		if (attached) mFilterTabs->removeTabPanel(mShowHiddenCustomTabPanel);
		return;
	}
	if (attached) return;
	if (mAddCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mAddCustomTabPanel) >= 0)
		mFilterTabs->removeTabPanel(mAddCustomTabPanel);
    {
        mFilterTabs->addTabPanel(mShowHiddenCustomTabPanel, "▼", false, NULL, this);
        mFilterTabs->setTabPrecommitChangeCallback(mShowHiddenCustomTabPanel, onShowHiddenCustomTabPrecommit);
        mFilterTabs->setTabButtonTooltip(mShowHiddenCustomTabPanel, "Restore a hidden custom inventory tab");
    }
    if (mAddCustomTabPanel)
	{
	    mFilterTabs->addTabPanel(mAddCustomTabPanel, "+", false,
	    NULL, this);
	    mFilterTabs->setTabPrecommitChangeCallback(mAddCustomTabPanel, onAddCustomTabPrecommit);
	    mFilterTabs->setTabButtonTooltip(mAddCustomTabPanel, "Add custom inventory filter tab");
	}
}

void LLFloaterInventory::contextRenameCustomTab(void* data)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (!self || !self->isCustomFilterTab(self->mContextCustomFilterPanel)) return;
	if (self->mHiddenCustomFilterPanels.count(self->mContextCustomFilterPanel)) return;
	self->mFilterTabs->selectTabPanel(self->mContextCustomFilterPanel);
	self->mActivePanel = self->mContextCustomFilterPanel;
	self->renameCustomFilterTab();
}

void LLFloaterInventory::contextCloneCustomTab(void* data)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (!self || !self->isCustomFilterTab(self->mContextCustomFilterPanel)) return;
	if (self->mHiddenCustomFilterPanels.count(self->mContextCustomFilterPanel)) return;
	self->mFilterTabs->selectTabPanel(self->mContextCustomFilterPanel);
	self->mActivePanel = self->mContextCustomFilterPanel;
	self->cloneCustomFilterTab();
}

void LLFloaterInventory::contextHideCustomTab(void* data)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (self && self->isCustomFilterTab(self->mContextCustomFilterPanel))
		self->hideCustomFilterTab(self->mContextCustomFilterPanel);
}

void LLFloaterInventory::contextCloseCustomTab(void* data)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (self && self->isCustomFilterTab(self->mContextCustomFilterPanel))
		self->requestCloseCustomFilterTab(self->mContextCustomFilterPanel);
}

void LLFloaterInventory::restoreHiddenCustomTabAction(void* data)
{
	HiddenTabAction* action = static_cast<HiddenTabAction*>(data);
	if (!action) return;
	LLFloaterInventory* self =
		dynamic_cast<LLFloaterInventory*>(action->owner.get());
	if (!self) return;
	for (LLInventoryPanel* panel : self->mCustomFilterPanels)
	{
		if (panel && panel->getName() == action->panel_name &&
			self->mHiddenCustomFilterPanels.count(panel))
		{
			self->showCustomFilterTab(panel);
			break;
		}
	}
}

void LLFloaterInventory::showHiddenCustomTabMenu()
{
	if (!mFilterTabs || mHiddenCustomFilterPanels.empty() ||
		!gMenuHolderp) return;
	if (mHiddenCustomTabMenu)
	{
		mHiddenCustomTabMenu->die();
		mHiddenCustomTabMenu = nullptr;
	}
	// Previous popup has closed; release its action objects before rebuilding.
	mHiddenTabActions.clear();
	LLMenuGL* menu = new LLMenuGL("ChaosHiddenInventoryTabs",
		"Hidden inventory tabs");
	mHiddenCustomTabMenu = menu;
	gMenuHolderp->addChild(menu);
	menu->setVisible(false);
	for (LLInventoryPanel* panel : mCustomFilterPanels)
	{
		if (!panel || !mHiddenCustomFilterPanels.count(panel)) continue;
		auto action = std::make_unique<HiddenTabAction>();
		action->owner = getHandle();
		action->panel_name = panel->getName();
		const std::string label = mCustomTabLabels[panel];
		menu->append(new LLMenuItemCallGL(label, restoreHiddenCustomTabAction,
			NULL, action.get()));
		mHiddenTabActions.push_back(std::move(action));
	}
	LLButton* selector = mShowHiddenCustomTabPanel ?
		mFilterTabs->getChild<LLButton>(
			(mShowHiddenCustomTabPanel->getName() + " tab").c_str(), true, false) : nullptr;
	const S32 x = selector ? selector->getRect().mLeft : 0;
	const S32 y = selector ? selector->getRect().mBottom : 0;
	LLMenuGL::showPopup(mFilterTabs, menu, x, y);
}

void LLFloaterInventory::saveCustomFilterTabs()
{
	// Never overwrite existing account data during a partial/failed load.
	if (mLoadingCustomFilterTabs || !mCustomFilterTabsLoadOK) return;
	LLSD saved = LLSD::emptyArray();
	for (LLInventoryPanel* panel : mCustomFilterPanels)
	{
		if (!panel) continue;
		const S32 index = mFilterTabs->getIndexForPanel(panel);
		if (index >= 0) mCustomTabLabels[panel] = mFilterTabs->getPanelTitle(index);
		LLSD entry;
		entry["name"] = mCustomTabLabels[panel];
		entry["hidden"] = mHiddenCustomFilterPanels.count(panel) != 0;
		LLSD filter;
		panel->getFilter()->toLLSD(filter);
		entry["filter"] = filter;
		saved.append(entry);
	}
	const std::string filename = gDirUtil.getFullPath(
		LL_PATH_PER_ACCOUNT, "chaos_custom_inventory_tabs.xml");
	llofstream output(filename.c_str());
	if (!output.is_open() || !LLSDSerialize::toPrettyXML(saved, output))
	{
		llwarns << "Could not save custom inventory tabs to " << filename << llendl;
	}
}

void LLFloaterInventory::loadCustomFilterTabs()
{
	const std::string filename = gDirUtil.getFullPath(
		LL_PATH_PER_ACCOUNT, "chaos_custom_inventory_tabs.xml");
	llifstream input(filename.c_str());
	if (!input.is_open()) return;
	LLSD saved;
	if (LLSDSerialize::fromXML(saved, input) <= 0 || !saved.isArray())
	{
		mCustomFilterTabsLoadOK = false;
		llwarns << "Invalid custom inventory tab state: " << filename << llendl;
		return;
	}
	mLoadingCustomFilterTabs = true;
	if (saved.size() > 64) mCustomFilterTabsLoadOK = false;
	for (LLSD::array_const_iterator it = saved.beginArray();
		 it != saved.endArray() && mCustomFilterPanels.size() < 64; ++it)
	{
		const LLSD& entry = *it;
		if (!entry.isMap() || !entry.has("name") || !entry.has("filter"))
		{
			mCustomFilterTabsLoadOK = false;
			continue;
		}
		const std::string name = entry["name"].asString();
		if (name.empty() || name.size() > 128 || !entry["filter"].isMap())
		{
			mCustomFilterTabsLoadOK = false;
			continue;
		}
		LLInventoryPanel* panel = createCustomFilterTab(name);
		if (!panel)
		{
			mCustomFilterTabsLoadOK = false;
			continue;
		}
		LLSD filter = entry["filter"];
		panel->getFilter()->fromLLSD(filter);
		panel->getFilter()->setModified();
		if (entry.has("hidden") && entry["hidden"].asBoolean())
			hideCustomFilterTab(panel);
	}
	mFilterTabs->selectTabPanel(mEverythingPanel);
	mActivePanel = mEverythingPanel;
	mLoadingCustomFilterTabs = false;
}

bool LLFloaterInventory::postBuild()
{
	mSavedFolderState = new LLSaveFolderState();
	mSavedFolderState->setApply(false);

	mFilterTabs = getChild<LLTabContainer>("inventory filter tabs");

	U32 default_sort_order = gSavedSettings.getU32("InventorySortOrder");

	// Set up the default inv. panel/filter settings.
	mEverythingPanel = getChild<LLInventoryPanel>("All Items");
	mEverythingPanel->setSortOrder(default_sort_order);
	mEverythingPanel->getFilter()->markDefault();
	mEverythingPanel->getRootFolder()->applyFunctorRecursively(*mSavedFolderState);
	mEverythingPanel->setSelectCallback(onSelectionChange, mEverythingPanel);
	mFilterTabs->setTabChangeCallback(mEverythingPanel, onFilterSelected);
	mFilterTabs->setTabUserData(mEverythingPanel, this);
	mActivePanel = mEverythingPanel;

	mRecentPanel = getChild<LLInventoryPanel>("Recent Items", true, false);
	if (mRecentPanel)
	{
		U32 sort_order = gSavedSettings.getU32("RecentItemsSortOrder");
		mRecentPanel->setSinceLogoff(true);
		mRecentPanel->setSortOrder(sort_order);
		mRecentPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
		mRecentPanel->getFilter()->markDefault();
		mRecentPanel->setSelectCallback(onSelectionChange, mRecentPanel);
		mFilterTabs->setTabChangeCallback(mRecentPanel, onFilterSelected);
		mFilterTabs->setTabUserData(mRecentPanel, this);
	}

	mWornPanel = getChild<LLInventoryPanel>("Worn Items", true, false);
	if (mWornPanel)
	{
		U32 sort_order = gSavedSettings.getU32("WornItemsSortOrder");
		mWornPanel->setSortOrder(sort_order);
		mWornPanel->setShowFolderState(LLInventoryFilter::SHOW_NON_EMPTY_FOLDERS);
		mWornPanel->getFilter()->markDefault();
		mWornPanel->setFilterWorn(true);
		mWornPanel->setSelectCallback(onSelectionChange, mWornPanel);
		mFilterTabs->setTabChangeCallback(mWornPanel, onFilterSelected);
		mFilterTabs->setTabUserData(mWornPanel, this);
	}

	mLastOpenPanel = getChild<LLInventoryPanel>("Last Open", true, false);
	if (mLastOpenPanel)
	{
		mLastOpenPanel->setSortOrder(default_sort_order);
		mLastOpenPanel->getFilter()->markDefault();
		mLastOpenPanel->setFilterLastOpen(true);
		mLastOpenPanel->setFilterShowLinks(true);
		mLastOpenPanel->setSelectCallback(onSelectionChange, mLastOpenPanel);
		mFilterTabs->setTabChangeCallback(mLastOpenPanel, onFilterSelected);
		mFilterTabs->setTabUserData(mLastOpenPanel, this);
	}

	mSearchEditor = getChild<LLSearchEditor>("inventory search editor");
	mSearchEditor->setSearchCallback(onSearchEdit, this);
	childSetAction("collapse_all", collapseInventoryFolders, this);
	childSetAction("expand_all", expandInventoryFolders, this);

	mLockLastOpenCheck = getChild<LLCheckBoxCtrl>("lock_last_open");
	mLockLastOpenCheck->setCommitCallback(onCommitLockLastOpenCheck);
	mLockLastOpenCheck->setCallbackUserData(this);
	mLockLastOpenCheck->setVisible(false);

	mNewSettingsMenuItem = getChild<LLView>("New Settings");
	mNewMaterialMenuItem = getChild<LLView>("New Material");

	// Permanent Firestorm-style add-tab and hidden-tab controls in Chaos.
	mShowHiddenCustomTabPanel = new LLPanel("ChaosCustomTabShowHidden",
		mEverythingPanel->getRect(), true);
	mAddCustomTabPanel = new LLPanel("ChaosCustomTabAdd",
		mEverythingPanel->getRect(), true);
	mFilterTabs->addTabPanel(mAddCustomTabPanel, "+", false,
		NULL, this);
		mFilterTabs->setTabPrecommitChangeCallback(mAddCustomTabPanel, onAddCustomTabPrecommit);
		mFilterTabs->setTabButtonTooltip(mAddCustomTabPanel, "Add custom inventory filter tab");
	// Restore only user-defined tabs; keep the original four tabs untouched.
	loadCustomFilterTabs();
	return true;
}

// Destroys the object
LLFloaterInventory::~LLFloaterInventory()
{
	saveCustomFilterTabs();
	// Save the filters state.
	LLInventoryFilter* filter = mEverythingPanel->getFilter();

	LLSD filter_state;
	filter->toLLSD(filter_state);
	LLSD filter_root;
	filter_root[filter->getName()] = filter_state;

	if (mRecentPanel)
	{
		LLInventoryFilter* filter = mRecentPanel->getFilter();
		LLSD filter_state;
		filter->toLLSD(filter_state);
		filter_root[filter->getName()] = filter_state;
	}

	if (mWornPanel)
	{
		LLInventoryFilter* filter = mWornPanel->getFilter();
		LLSD filter_state;
		filter->toLLSD(filter_state);
		filter_root[filter->getName()] = filter_state;
	}

	if (mLastOpenPanel)
	{
		LLInventoryFilter* filter = mLastOpenPanel->getFilter();
		LLSD filter_state;
		filter->toLLSD(filter_state);
		filter_root[filter->getName()] = filter_state;
	}

	std::ostringstream filter_sream;
	filter_sream << gDirUtil.getFullPath(LL_PATH_PER_ACCOUNT, "filters.xml");
	llofstream filters_file(filter_sream.str().c_str());
	if (!filters_file.is_open() ||
		!LLSDSerialize::toPrettyXML(filter_root, filters_file))
	{
		llwarns << "Could not write to filters save file "
				<< filter_sream.str() << llendl;
	}
	else
	{
		filters_file.close();
	}

	std::vector<LLFloaterInventory*>::iterator end = sActiveViews.end();
	std::vector<LLFloaterInventory*>::iterator it =
		std::find(sActiveViews.begin(), end, this);
	if (it != end)
	{
		sActiveViews.erase(it);
	}

	gInventory.removeObserver(this);

	// Detached hidden tabs aren't owned by the tab container: release them.
	for (LLInventoryPanel* hidden_panel : mHiddenCustomFilterPanels)
		delete hidden_panel;
	mHiddenCustomFilterPanels.clear();
	if (mShowHiddenCustomTabPanel &&
		mFilterTabs->getIndexForPanel(mShowHiddenCustomTabPanel) < 0)
		delete mShowHiddenCustomTabPanel;
	if (mCustomTabContextMenu) mCustomTabContextMenu->die();
	if (mHiddenCustomTabMenu) mHiddenCustomTabMenu->die();

	delete mSavedFolderState;
}

void LLFloaterInventory::draw()
{
//MK
	if (gRLenabled && gRLInterface.mContainsShowinv)
	{
		setVisible(false);
		return;
	}
//mk
 	if (LLInventoryModelFetch::getInstance()->isEverythingFetched())
	{
		if (mLastCount != gInventory.getItemCount())
		{
			mLastCount = gInventory.getItemCount();
			mLastCountString.clear();
			LLLocale locale(LLLocale::USER_LOCALE);
			LLLocale::getIntegerString(mLastCountString, mLastCount);
		}
		setTitle("Inventory (" + mLastCountString + " items)" + mFilterText);
	}
	if (mActivePanel)
	{
		mSearchEditor->setText(mActivePanel->getFilterSubString());

		LLMarketplace::updateAllCounts();
	}
	// Enable/disable inventory items creation menu entries depending on
	// available features in the agent region. HB
	mNewSettingsMenuItem->setEnabled(gAgent.hasInventorySettings());
	mNewMaterialMenuItem->setEnabled(gAgent.hasInventoryMaterial());

	LLFloater::draw();
}

void LLFloaterInventory::startSearch()
{
	// This forces focus to line editor portion of search editor
	mSearchEditor->focusFirstItem(true);
}

//virtual
void LLFloaterInventory::setVisible(bool visible)
{
	gSavedSettings.setBool("ShowInventory", visible);
	LLFloater::setVisible(visible);
	if (visible && LLStartUp::isLoggedIn())
	{
		static bool warn = true;
		// Verify that the Marketplace is initialized.
		LLMarketplace::setup(warn);
		warn = false;	// Warn only once per session
	}
}

//virtual
void LLFloaterInventory::onClose(bool app_quitting)
{
	if (sActiveViews.size() > 1)
	{
		destroy();
		return;
	}

	if (!app_quitting)
	{
		gSavedSettings.setBool("ShowInventory", false);
	}

	// Clear filters, but save user's folder state first
	if (mActivePanel && !mActivePanel->getRootFolder()->isFilterModified())
	{
		mSavedFolderState->setApply(false);
		mActivePanel->getRootFolder()->applyFunctorRecursively(*mSavedFolderState);
	}

#if 0
	onClearSearch(this);
#endif

	// Close the temporary thumbnail view floater, if open.
	HBFloaterThumbnail::hideInstance();

	// Pass up
	LLFloater::setVisible(false);
}

bool LLFloaterInventory::handleRightMouseDown(S32 x, S32 y, MASK mask)
{
	if (mFilterTabs && gMenuHolderp)
	{
		S32 tab_x = 0, tab_y = 0;
		if (localPointToOtherView(x, y, &tab_x, &tab_y, mFilterTabs))
		{
			for (LLInventoryPanel* panel : mCustomFilterPanels)
			{
				if (!panel || mHiddenCustomFilterPanels.count(panel)) continue;
				LLButton* button = mFilterTabs->getChild<LLButton>(
					(panel->getName() + " tab").c_str(), true, false);
				if (!button || !button->getVisible() ||
					!button->getRect().pointInRect(tab_x, tab_y))
					continue;
				mContextCustomFilterPanel = panel;
				if (mCustomTabContextMenu)
				{
					mCustomTabContextMenu->die();
					mCustomTabContextMenu = nullptr;
				}
				LLMenuGL* menu = new LLMenuGL(
					"ChaosInventoryCustomTabContext", "Custom inventory tab");
				mCustomTabContextMenu = menu;
				gMenuHolderp->addChild(menu);
				menu->setVisible(false);
				menu->append(new LLMenuItemCallGL("Rename tab",
					contextRenameCustomTab, NULL, this));
				menu->append(new LLMenuItemCallGL("Clone tab",
					contextCloneCustomTab, NULL, this));
				menu->append(new LLMenuItemCallGL("Hide tab",
					contextHideCustomTab, NULL, this));
				menu->append(new LLMenuItemCallGL("Close tab",
					contextCloseCustomTab, NULL, this));
				LLMenuGL::showPopup(this, menu, x, y);
				return true;
			}
		}
	}
	return LLFloater::handleRightMouseDown(x, y, mask);
}

bool LLFloaterInventory::handleKeyHere(KEY key, MASK mask)
{
	LLFolderView* root_folder = mActivePanel ? mActivePanel->getRootFolder()
											 : NULL;
	if (root_folder)
	{
		// First check for user accepting current search results
		if (mSearchEditor->hasFocus() && mask == MASK_NONE &&
			(key == KEY_RETURN || key == KEY_DOWN))
		{
			// Move focus to inventory proper
			root_folder->setFocus(true);
			root_folder->scrollToShowSelection();
			return true;
		}

		if (root_folder->hasFocus() && key == KEY_UP)
		{
			startSearch();
		}
	}

	return LLFloater::handleKeyHere(key, mask);
}

void LLFloaterInventory::changed(U32 mask)
{
	std::ostringstream title;
	title << "Inventory";
 	if (LLInventoryModelFetch::getInstance()->backgroundFetchActive())
	{
		LLLocale locale(LLLocale::USER_LOCALE);
		std::string item_count_string;
		LLLocale::getIntegerString(item_count_string,
								   gInventory.getItemCount());
		title << " (Fetched " << item_count_string << " items...)";
	}
	title << mFilterText;
	setTitle(title.str());
}

//static
LLFloaterInventory* LLFloaterInventory::showAgentInventory()
{
	if (gDisconnected)
	{
		return NULL;
	}

//MK
	if (gRLenabled && gRLInterface.mContainsShowinv)
	{
		return NULL;
	}
//mk

	LLFloaterInventory* inv = LLFloaterInventory::getActiveFloater();
	if (!inv && !gAgent.cameraMouselook())
	{
		// Create one.
		inv = new LLFloaterInventory("Inventory", "FloaterInventoryRect",
									 &gInventory);
		inv->open();
		// Keep on screen
		gFloaterViewp->adjustToFitScreen(inv);

		gSavedSettings.setBool("ShowInventory", true);
	}

	if (inv)
	{
		// Make sure it is in front and it makes a noise
		inv->setTitle("Inventory");
		inv->open();
	}

	return inv;
}

//static
LLFloaterInventory* LLFloaterInventory::getActiveFloater()
{
	LLFloaterInventory* self = NULL;

	S32 count = sActiveViews.size();
	if (count > 0)
	{
		self = sActiveViews[0];
		S32 z_order = gFloaterViewp->getZOrder(self);
		S32 z_next = 0;
		for (S32 i = 1; i < count; ++i)
		{
			LLFloaterInventory* next_invp = sActiveViews[i];
			z_next = gFloaterViewp->getZOrder(next_invp);
			if (z_next < z_order)
			{
				self = next_invp;
				z_order = z_next;
			}
		}
	}

	return self;
}

//static
void LLFloaterInventory::toggleVisibility(void*)
{
//MK
	if (gRLenabled && gRLInterface.mContainsShowinv)
	{
		return;
	}
//mk
	S32 count = sActiveViews.size();
	if (count == 0)
	{
		showAgentInventory();
	}
	else if (count == 1)
	{
		if (sActiveViews[0]->getVisible())
		{
			sActiveViews[0]->close();
			gSavedSettings.setBool("ShowInventory", false);
		}
		else
		{
			showAgentInventory();
		}
	}
	else
	{
		// With more than one open, we know at least one is visible. Close the
		// last spawned one.
		sActiveViews.back()->close();
	}
}

//static
void LLFloaterInventory::cleanup()
{
	for (S32 i = 0, count = sActiveViews.size(); i < count; ++i)
	{
		sActiveViews[i]->destroy();
	}
}

//static
void LLFloaterInventory::onCommitLockLastOpenCheck(LLUICtrl* ctrlp,
												   void* datap)
{
	LLFloaterInventory* self = (LLFloaterInventory*)datap;
	LLCheckBoxCtrl* checkp = (LLCheckBoxCtrl*)ctrlp;
	if (self && checkp)
	{
		bool locked = checkp->get();
		self->mEverythingPanel->setLastOpenLocked(locked);
		self->mRecentPanel->setLastOpenLocked(locked);
		self->mWornPanel->setLastOpenLocked(locked);
		self->mLastOpenPanel->setLastOpenLocked(locked);
	}
}

void LLFloaterInventory::setFilterTextFromFilter()
{
	mFilterText = mActivePanel->getFilter()->getFilterText();
}

const std::string LLFloaterInventory::getFilterSubString()
{
	return mActivePanel->getFilterSubString();
}

void LLFloaterInventory::setFilterSubString(const std::string& string)
{
	mActivePanel->setFilterSubString(string);
}

void LLFloaterInventory::toggleFindOptions()
{
	LLFloater* floaterp = getInvFilters();
	if (floaterp)
	{
		floaterp->close();
		mControls["Inventory.ShowFilters"]->setValue(false);
		return;
	}

	LLRect rect(getRect().mLeft - INV_FINDER_WIDTH, getRect().mTop,
				getRect().mLeft, getRect().mTop - INV_FINDER_HEIGHT);
	LLFloaterInventoryFilters* filtersp =
		new LLFloaterInventoryFilters("Inventory Finder", rect, this);
	mInvFiltersHandle = filtersp->getHandle();
	filtersp->open();
	addDependentFloater(mInvFiltersHandle);

	mControls["Inventory.ShowFilters"]->setValue(true);
}

void LLFloaterInventory::updateSearchControls()
{
	U32 flags = mActivePanel ? mActivePanel->getSearchType() : 0;
	getControl("Inventory.SearchName")->setValue(flags == 0 || (flags & 1));
	getControl("Inventory.SearchDesc")->setValue((flags & 2) != 0);
	getControl("Inventory.SearchCreator")->setValue((flags & 4) != 0);
	bool show_links = mActivePanel && mActivePanel->getFilterShowLinks();
	getControl("Inventory.SearchShowLinks")->setValue(show_links);
}

void LLFloaterInventory::updateSortControls()
{
	U32 order = mActivePanel ? mActivePanel->getSortOrder()
							 : gSavedSettings.getU32("InventorySortOrder");
	bool sort_by_date = order & LLInventoryFilter::SO_DATE;
	bool folders_by_name = order & LLInventoryFilter::SO_FOLDERS_BY_NAME;
	bool sys_folders_on_top = order & LLInventoryFilter::SO_SYSTEM_FOLDERS_TO_TOP;
	getControl("Inventory.SortByDate")->setValue(sort_by_date);
	getControl("Inventory.SortByName")->setValue(!sort_by_date);
	getControl("Inventory.FoldersAlwaysByName")->setValue(folders_by_name);
	getControl("Inventory.SystemFoldersToTop")->setValue(sys_folders_on_top);
}

//static
void LLFloaterInventory::onClearSearch(void* datap)
{
	LLFloaterInventory* self = (LLFloaterInventory*)datap;
	if (!self) return;

	LLInventoryPanel* panelp = self->mActivePanel;
	if (panelp)
	{
		panelp->setFilterSubString(LLStringUtil::null);
		panelp->setFilterTypes(0xffffffff);
	}

	LLFloater* filters = self->getInvFilters();
	if (filters)
	{
		LLFloaterInventoryFilters::selectAllTypes(filters);
	}

	// Re-open folders that were initially open
	if (panelp)
	{
		self->mSavedFolderState->setApply(true);
		panelp->getRootFolder()->applyFunctorRecursively(*self->mSavedFolderState);
		LLOpenFoldersWithSelection opener;
		panelp->getRootFolder()->applyFunctorRecursively(opener);
		panelp->getRootFolder()->scrollToShowSelection();
	}
}

//static
void LLFloaterInventory::onSearchEdit(const std::string& search_string,
									  void* datap)
{
	LLFloaterInventory* self = (LLFloaterInventory*)datap;
	if (!self)
	{
		return;
	}

	if (search_string.empty())
	{
		onClearSearch(datap);
	}

	LLInventoryPanel* panelp = self->mActivePanel;
	if (!panelp)
	{
		return;
	}

	std::string filter_text = search_string;
	std::string uc_search_string = filter_text;
	LLStringUtil::toUpper(uc_search_string);
	if (panelp->getFilterSubString().empty() && uc_search_string.empty())
	{
		// Current filter and new filter empty, do nothing
		return;
	}

	// Save current folder open state if no filter currently applied
	if (!panelp->getRootFolder()->isFilterModified())
	{
		self->mSavedFolderState->setApply(false);
		panelp->getRootFolder()->applyFunctorRecursively(*self->mSavedFolderState);
	}

	// Set new filter string
	panelp->setFilterSubString(uc_search_string);
}

// The legacy tab control invokes precommit *before* changing the active panel.
// The "+" and hidden-selector panels must never become the active inventory.
void LLFloaterInventory::onAddCustomTabPrecommit(void* data, bool)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (!self) return;
	// Never remove/reinsert a tab inside LLTabContainer::selectTab(): its
	// selected tuple pointer is still in use until the click handler returns.
	// Do the native panel insertion on the next idle tick instead.
	LLHandle<LLView> handle = self->getHandle();
	doOnIdleOneTime([handle]()
	{
		LLFloaterInventory* inv = dynamic_cast<LLFloaterInventory*>(handle.get());
		if (inv && inv->mFilterTabs && inv->mEverythingPanel)
		{
			inv->addCustomFilterTab();
		}
	});
}

void LLFloaterInventory::onShowHiddenCustomTabPrecommit(void* data, bool)
{
	LLFloaterInventory* self = static_cast<LLFloaterInventory*>(data);
	if (self) self->showHiddenCustomTabMenu();
}

//static
void LLFloaterInventory::onFilterSelected(void* datap, bool)
{
	LLFloaterInventory* self = (LLFloaterInventory*)datap;
	if (!self)
	{
		return;
	}

	// Show a menu of individually hidden custom tabs from the ▼ control.
	if (self->mFilterTabs->getCurrentPanel() == self->mShowHiddenCustomTabPanel)
	{
		self->mFilterTabs->selectTabPanel(self->mEverythingPanel);
		self->showHiddenCustomTabMenu();
		return;
	}
	// The + tab creates a new native inventory filter tab.
	if (self->mFilterTabs->getCurrentPanel() == self->mAddCustomTabPanel)
	{
		self->addCustomFilterTab();
		if (self->mFilterTabs->getCurrentPanel() == self->mAddCustomTabPanel)
			self->mFilterTabs->selectTabPanel(self->mEverythingPanel);
		return;
	}
	// Find my index
	LLInventoryPanel* panelp =
		(LLInventoryPanel*)self->mFilterTabs->getCurrentPanel();
	self->mActivePanel = panelp;
	if (!panelp)
	{
		return;
	}

	LLInventoryFilter* filterp = panelp->getFilter();
	if (filterp)
	{
		if (filterp->isActive())
		{
			// If our filter is active we may be the first thing requiring a
			// fetch in this folder, so we better start it here.
			const LLFolderViewEventListener* listenerp =
				panelp->getRootFolder()->getListener();
			if (listenerp)
			{
				const LLUUID& cat_id = listenerp->getUUID();
				LLInventoryModelFetch::getInstance()->start(cat_id);
			}
		}

		bool is_last_open = panelp == self->mLastOpenPanel;
		if (is_last_open && panelp->makeLastOpenCurrent())
		{
			// Force a refresh of the Last Open tab
			filterp->setLastOpenID(panelp->getLastOpenID());
			filterp->setModified();
		}
		self->mLockLastOpenCheck->setVisible(is_last_open);

		LLFloaterInventoryFilters* filters = self->getInvFilters();
		if (filters)
		{
			filters->changeFilter(filterp);
		}
	}

	self->setFilterTextFromFilter();
	self->updateSearchControls();
	self->updateSortControls();
}

//static
void LLFloaterInventory::onSelectionChange(LLFolderView* folderp, bool, void*)
{
	// If auto-selecting a new user-created asset and preparing to rename
	if (folderp && folderp->needsAutoRename())
	{
		folderp->setNeedsAutoRename(false);
		if (folderp->getSelectedItems().size())
		{
			// New asset is visible and selected
			folderp->startRenamingSelectedItem();
		}
	}
}

bool LLFloaterInventory::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
										   EDragAndDropType cargo_type,
										   void* cargo_data,
										   EAcceptance* accept,
										   std::string& tooltip_msg)
{
	// Check to see if we are auto scrolling from the last frame
	LLInventoryPanel* panelp = (LLInventoryPanel*)getActivePanel();
	if (mFilterTabs && panelp && panelp->getScrollableContainer())
	{
		if (panelp->getScrollableContainer()->needsToScroll(x, y,
														    LLScrollableContainer::VERTICAL))
		{
			mFilterTabs->startDragAndDropDelayTimer();
		}
	}

	return LLFloater::handleDragAndDrop(x, y, mask, drop, cargo_type,
										cargo_data, accept, tooltip_msg);
}
