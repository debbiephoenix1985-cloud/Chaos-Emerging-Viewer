/**
 * @file lltoolbar.cpp
 * @author James Cook, Richard Nelson
 * @brief Large friendly buttons at bottom of screen.
 *
 * $LicenseInfo:firstyear=2002&license=viewergpl$
 *
 * Copyright (c) 2002-2009, Linden Research, Inc.
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

#include "lltoolbar.h"
extern void chaosToggleQuickPreferences();

#include "imageids.h"
#include "llbutton.h"
#include "llparcel.h"
#include "lluictrlfactory.h"

#include "llagent.h"
#include "llfirstuse.h"
#include "llfloaterchatterbox.h"
#include "llfloaterfriends.h"
#include "hbfloaterteleporthistory.h"
#include "hbfloaterareasearch.h"
#include "llfloaterinventory.h"
#include "hbfloaterwearablefavorites.h"
#include "ao.h" // Existing Firestorm-based AO editor; no AO engine changes.
#include "llfloaterminimap.h"
#include "hbfloatersearch.h"
#include "hbfloaterradar.h"
#include "llfloatersnapshot.h"
#include "llfloaterworldmap.h"
#include "llfloaterregioninfo.h"
#include "llfloaterstats.h"
#include "llfloatercamera.h"
//MK
#include "mkrlinterface.h"
//mk
#include "lltooldraganddrop.h"
#include "lltoolgrab.h"
#include "lltoolmgr.h"
#include "llviewermenu.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"

#if LL_DARWIN

#include "llresizehandle.h"

// This class draws like an LLResizeHandle but has no interactivity. It's just
// there to provide a cue to the user that the lower right corner of the window
// functions as a resize handle.
class LLFakeResizeHandle final : public LLResizeHandle
{
public:
	LLFakeResizeHandle(const std::string& name, const LLRect& rect,
					   S32 min_width, S32 min_height,
					   ECorner corner = RIGHT_BOTTOM)
	:	LLResizeHandle(name, rect, min_width, min_height, corner)
	{
	}

	bool handleHover(S32 x, S32 y, MASK mask) override		{ return false; };
	bool handleMouseDown(S32 x, S32 y, MASK mask) override	{ return false; };
	bool handleMouseUp(S32 x, S32 y, MASK mask) override	{ return false; };
};

#endif // LL_DARWIN

//
// Globals
//

// Instance created in LLViewerWindow::initWorldUI()
LLToolBar* gToolBarp = NULL;

//
// Statics
//
F32	LLToolBar::sInventoryAutoOpenTime = 1.f;

//
// Functions
//

LLToolBar::LLToolBar(const LLRect& rect)
:	LLPanel("tool bar", rect, BORDER_NO)
#if LL_DARWIN
	, mResizeHandle(NULL)
#endif // LL_DARWIN
{
	llassert_always(gToolBarp == NULL);	// Only one instance allowed

	setIsChrome(true);
	setFollows(FOLLOWS_LEFT | FOLLOWS_RIGHT | FOLLOWS_BOTTOM);
	LLUICtrlFactory::getInstance()->buildPanel(this, "panel_toolbar.xml");
	setFocusRoot(true);
	gToolBarp = this;
}

bool LLToolBar::postBuild()
{
	mChatButton = getChild<LLButton>("chat_btn");
	mChatButton->setClickedCallback(onClickChat, this);
	mChatButton->setControlName("ChatVisible", NULL);

	mIMButton = getChild<LLButton>("quickprefs_btn");
	mIMButton->setClickedCallback(onClickIM, this);
	mIMButton->setControlName("", NULL);

	mFriendsButton = getChild<LLButton>("teleport_history_btn");
	mFriendsButton->setClickedCallback(onClickFriends, this);
	mFriendsButton->setControlName("", NULL);

	mAreaSearchButton = getChild<LLButton>("area_search_btn");
	mAreaSearchButton->setClickedCallback(onClickAreaSearch, this);
	mAreaSearchButton->setControlName("", NULL);

	mFlyButton = getChild<LLButton>("fly_btn");
	mFlyButton->setClickedCallback(onClickFly, this);
	mFlyButton->setControlName("FlyBtnState", NULL);

	mSnapshotButton = getChild<LLButton>("snapshot_btn");
	mSnapshotButton->setClickedCallback(onClickSnapshot, this);
	mSnapshotButton->setControlName("", NULL);

	mSearchButton = getChild<LLButton>("directory_btn");
	mSearchButton->setClickedCallback(onClickSearch, this);
	mSearchButton->setControlName("ShowSearch", NULL);

	mBuildButton = getChild<LLButton>("build_btn");
	mBuildButton->setClickedCallback(onClickBuild, this);
	mBuildButton->setControlName("BuildBtnState", NULL);

	mRadarButton = getChild<LLButton>("radar_btn");
	mRadarButton->setClickedCallback(onClickRadar, this);
	mRadarButton->setControlName("ShowRadar", NULL);

	mMiniMapButton = getChild<LLButton>("minimap_btn");
	mMiniMapButton->setClickedCallback(onClickMiniMap, this);
	mMiniMapButton->setControlName("ShowMiniMap", NULL);

	mMapButton = getChild<LLButton>("map_btn");
	mMapButton->setClickedCallback(onClickMap, this);
	mMapButton->setControlName("ShowWorldMap", NULL);

	mRegionInfoButton = getChild<LLButton>("region_info_btn");
	mRegionInfoButton->setClickedCallback(onClickRegionInfo, this);
	mRegionInfoButton->setControlName("", NULL);

	mStatisticsButton = getChild<LLButton>("statistics_btn");
	mStatisticsButton->setClickedCallback(onClickStatistics, this);
	mStatisticsButton->setControlName("", NULL);

	mCameraControlsButton = getChild<LLButton>("camera_controls_btn");
	mCameraControlsButton->setClickedCallback(onClickCameraControls, this);
	mCameraControlsButton->setControlName("", NULL);

	mInventoryButton = getChild<LLButton>("inventory_btn");
	mInventoryButton->setClickedCallback(onClickInventory, this);
	mInventoryButton->setControlName("ShowInventory", NULL);

	mFavoritesButton = getChild<LLButton>("wearable_favorites_btn");
	mFavoritesButton->setClickedCallback(onClickFavorites, this);
	mFavoritesButton->setControlName("", NULL);

	// Firestorm Vintage split AO control: add only the AO toggle and editor.
	mAOButton = getChild<LLButton>("chaos_ao_toggle_btn");
	mAOButton->setClickedCallback(onClickAOToggle, this);
	mAOButton->setControlName("", NULL);
	mAOEditorArrow = getChild<LLButton>("chaos_ao_editor_arrow_btn");
	mAOEditorArrow->setClickedCallback(onClickAOEditor, this);
	mAOEditorArrow->setControlName("", NULL);

	for (child_list_const_iter_t child_iter = getChildList()->begin();
		 child_iter != getChildList()->end(); ++child_iter)
	{
		LLView* view = *child_iter;
		LLButton* buttonp = dynamic_cast<LLButton*>(view);
		if (buttonp)
		{
			buttonp->setSoundFlags(LLView::SILENT);
		}
	}

#if LL_DARWIN
	if (mResizeHandle == NULL)
	{
		LLRect rect(0, 0, RESIZE_HANDLE_WIDTH, RESIZE_HANDLE_HEIGHT);
		mResizeHandle = new LLFakeResizeHandle(std::string(""), rect,
											   RESIZE_HANDLE_WIDTH,
											   RESIZE_HANDLE_HEIGHT);
		this->addChildAtEnd(mResizeHandle);
	}
#endif // LL_DARWIN

	layoutButtons();

	return true;
}

LLToolBar::~LLToolBar()
{
	// LLView destructor cleans up children
	gToolBarp = NULL;
}

bool LLToolBar::handleDragAndDrop(S32 x, S32 y, MASK mask, bool drop,
								  EDragAndDropType cargo_type,
								  void* cargo_data,
								  EAcceptance* accept,
								  std::string& tooltip_msg)
{
	LLFloaterInventory* floaterp = LLFloaterInventory::getActiveFloater();

	if (floaterp && floaterp->getVisible())
	{
		mInventoryAutoOpen = false;
	}
	else if (mInventoryButton->getRect().pointInRect(x, y))
	{
		if (mInventoryAutoOpen)
		{
			if (!(floaterp && floaterp->getVisible()) &&
				mInventoryAutoOpenTimer.getElapsedTimeF32() > sInventoryAutoOpenTime)
			{
				LLFloaterInventory::showAgentInventory();
			}
		}
		else
		{
			mInventoryAutoOpen = true;
			mInventoryAutoOpenTimer.reset();
		}
	}

	return LLPanel::handleDragAndDrop(x, y, mask, drop, cargo_type, cargo_data,
									  accept, tooltip_msg);
}

// static
void LLToolBar::toggle()
{
	if (gToolBarp)
	{
		bool show = gSavedSettings.getBool("ShowToolBar");
		gSavedSettings.setBool("ShowToolBar", !show);
		gToolBarp->setVisible(!show);
	}
}

// static
bool LLToolBar::isVisible()
{
	return gToolBarp && gToolBarp->getVisible();
}

void LLToolBar::layoutButtons()
{
	// Always spans whole window. JC
	constexpr S32 FUDGE_WIDTH_OF_SCREEN = 4;
	constexpr S32 PAD = 2;
	S32 width = gViewerWindowp->getWindowWidth() + FUDGE_WIDTH_OF_SCREEN;
	S32 count = getChildCount();
	if (!count) return;

	bool show = gSavedSettings.getBool("ShowChatButton");
	mChatButton->setVisible(show);
	if (!show) --count;

	// Cool Prefs > Quick Prefs uses the existing toolbar visibility setting.
	show = gSavedSettings.getBool("ShowIMButton");
	mIMButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowFriendsButton");
	mFriendsButton->setVisible(show);
	if (!show) --count;

	// Preserve the existing toolbar visibility preference after replacing Groups with Area Search.
	show = gSavedSettings.getBool("ShowGroupsButton");
	mAreaSearchButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowFlyButton");
	mFlyButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowSnapshotButton");
	mSnapshotButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowSearchButton");
	mSearchButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowBuildButton");
	mBuildButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowRadarButton");
	mRadarButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowMiniMapButton");
	mMiniMapButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowMapButton");
	mMapButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowRegionInfoButton");
	mRegionInfoButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowStatisticsButton");
	mStatisticsButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowCameraControlsButton");
	mCameraControlsButton->setVisible(show);
	if (!show) --count;

	show = gSavedSettings.getBool("ShowInventoryButton");
	mInventoryButton->setVisible(show);
	if (!show) --count;

	// The existing Favourite Wearables shortcut can be hidden or shown.
	show = gSavedSettings.getBool("ShowFavWearablesButton");
	mFavoritesButton->setVisible(show);
	if (!show) --count;

	// Both halves of the AO split control must always hide/show together.
	// Exclude BOTH halves from the ordinary toolbar button distribution;
	// the compact AO control is positioned separately on the far right.
	const bool show_ao = gSavedSettings.getBool("ShowAOButton");
	mAOButton->setVisible(show_ao);
	mAOEditorArrow->setVisible(show_ao);
	count -= 2;

	if (count < 1)
	{
		// No button in the toolbar !  Hide it.
		gSavedSettings.setBool("ShowToolBar", false);
		return;
	}

#if LL_DARWIN
	// this function may be called before postBuild(), in which case
	// mResizeHandle won't have been set up yet.
	if (mResizeHandle)
	{
		// a resize handle has been added as a child, increasing the count by
		// one.
		--count;

		if (!gWindowp->getFullscreen())
		{
			// Only when running in windowed mode on the Mac, leave room for a
			// resize widget on the right edge of the bar.
			width -= RESIZE_HANDLE_WIDTH;

			LLRect r;
			r.mLeft = width - PAD;
			r.mBottom = 0;
			r.mRight = r.mLeft + RESIZE_HANDLE_WIDTH;
			r.mTop = r.mBottom + RESIZE_HANDLE_HEIGHT;
			mResizeHandle->setRect(r);
			mResizeHandle->setVisible(true);
		}
		else
		{
			mResizeHandle->setVisible(false);
		}
	}
#endif // LL_DARWIN

	// We actually want to extend "PAD" pixels off the right edge of the
	// screen, such that the rightmost button is aligned.
	// The AO control is compact like Firestorm Vintage: 40px AO + 20px
	// arrow, rather than stretching to fill a normal toolbar segment.
	constexpr S32 AO_TOGGLE_WIDTH = 40;
	constexpr S32 AO_ARROW_WIDTH = 20;
	constexpr S32 AO_TOTAL_WIDTH = AO_TOGGLE_WIDTH + AO_ARROW_WIDTH;
	const S32 buttons_width = width - (show_ao ? AO_TOTAL_WIDTH + 2 * PAD : 0);
	F32 segment_width = (F32)(buttons_width + PAD) / (F32)count;
	S32 btn_width = S32(segment_width - PAD);

	// Evenly space all views
	S32 height = -1;
	S32 i = count - 1;
	for (child_list_const_iter_t child_iter = getChildList()->begin();
		 child_iter != getChildList()->end(); ++child_iter)
	{
		LLView* btn_view = *child_iter;
		LLButton* buttonp = dynamic_cast<LLButton*>(btn_view);
		// Neither AO half may be stretched by the normal toolbar layout.
		if (buttonp == mAOButton || buttonp == mAOEditorArrow) continue;
		if (buttonp && btn_view->getVisible())
		{
			if (height < 0)
			{
				height = btn_view->getRect().getHeight();
			}
			S32 x = ll_roundp(i * segment_width);
			S32 y = 0;
			LLRect r;
			r.setOriginAndSize(x, y, btn_width, height);
			btn_view->setRect(r);
			--i;
		}
	}
	// The two AO halves are joined edge to edge and fixed at 60 x 20.
	// Their visibility is already controlled together above.
	if (show_ao)
	{
		constexpr S32 AO_BOTTOM = 2;
		constexpr S32 AO_HEIGHT = 20;
		// Unlike ordinary toolbar slots, the AO split control must stay
		// entirely inside the visible toolbar, not its +4px layout fudge.
		// The toolbar panel rectangle may be narrower than the screen.
		// Existing buttons use the viewer window width for their coordinates.
		const S32 ao_right = width - FUDGE_WIDTH_OF_SCREEN;
		const S32 ao_left = ao_right - AO_TOTAL_WIDTH;
		mAOButton->setRect(LLRect(ao_left, AO_BOTTOM + AO_HEIGHT,
									  ao_left + AO_TOGGLE_WIDTH, AO_BOTTOM));
		mAOEditorArrow->setRect(LLRect(ao_left + AO_TOGGLE_WIDTH,
										 AO_BOTTOM + AO_HEIGHT,
										 ao_right, AO_BOTTOM));
	}
}

// virtual
void LLToolBar::reshape(S32 width, S32 height, bool called_from_parent)
{
	LLPanel::reshape(width, height, called_from_parent);

	layoutButtons();
}

// Per-frame updates of visibility
void LLToolBar::refresh()
{
	// Ensure we use setVisible() on startup:
	static S32 previous_visibility = -1;
	static LLCachedControl<bool> show_toolbar(gSavedSettings, "ShowToolBar");
	bool visible = show_toolbar && !gAgent.cameraMouselook();

	if (previous_visibility != (S32)visible)
	{
		setVisible(visible);
		if (visible)
		{
			// In case there would be no button to show, it would re-hide the
			// toolbar (on next frame)
			layoutButtons();
		}
		previous_visibility = (S32)visible;
	}
	if (!visible) return;

	// Only the new AO control responds to its Show/Hide preference.
	static S32 last_show_ao = -1;
	const S32 show_ao = gSavedSettings.getBool("ShowAOButton") ? 1 : 0;
	static S32 last_show_region_info = -1;
	static S32 last_show_statistics = -1;
	static S32 last_show_camera = -1;
	const S32 show_region_info = gSavedSettings.getBool("ShowRegionInfoButton") ? 1 : 0;
	const S32 show_statistics = gSavedSettings.getBool("ShowStatisticsButton") ? 1 : 0;
	const S32 show_camera = gSavedSettings.getBool("ShowCameraControlsButton") ? 1 : 0;
	if (last_show_ao != show_ao ||
		last_show_region_info != show_region_info ||
		last_show_statistics != show_statistics ||
		last_show_camera != show_camera)
	{
		last_show_ao = show_ao;
		last_show_region_info = show_region_info;
		last_show_statistics = show_statistics;
		last_show_camera = show_camera;
		layoutButtons();
	}
	mAOButton->setToggleState(gSavedPerAccountSettings.getBool("UseAO"));

	bool sitting = false;
	if (isAgentAvatarValid())
	{
		sitting = gAgentAvatarp->mIsSitting;
	}
	mFlyButton->setEnabled(!sitting &&
						   (gAgent.canFly() || gAgent.getFlying()));

//MK
	if (gRLenabled)
	{
		mRadarButton->setEnabled(!gRLInterface.mContainsShownames &&
								 !gRLInterface.mContainsShownametags);
		mMiniMapButton->setEnabled(!gRLInterface.mContainsShowminimap);
		mMapButton->setEnabled(!gRLInterface.mContainsShowworldmap &&
							   !gRLInterface.mContainsShowloc);
		mInventoryButton->setEnabled(!gRLInterface.mContainsShowinv);
		mFavoritesButton->setEnabled(!gRLInterface.mContainsShowinv);
	}
	else
	{
		mFavoritesButton->setEnabled(true);
	}
//mk

	mBuildButton->setEnabled(gViewerParcelMgr.allowAgentBuild());
}

// static
void LLToolBar::onClickChat(void* user_data)
{
	handle_chat(NULL);
}

// static
void LLToolBar::onClickIM(void* user_data)
{
	// The former IM slot now toggles Firestorm Quick Preferences.
	chaosToggleQuickPreferences();
}

// static
void LLToolBar::onClickFly(void*)
{
	gAgent.toggleFlying();
}

// static
void LLToolBar::onClickSnapshot(void*)
{
	LLFloaterSnapshot::show(NULL);
}

// static
void LLToolBar::onClickSearch(void*)
{
	HBFloaterSearch::toggle();
}

// static
void LLToolBar::onClickBuild(void*)
{
	gToolMgr.toggleBuildMode();
}

// static
void LLToolBar::onClickMiniMap(void*)
{
	LLFloaterMiniMap::toggleInstance();
}

// static
void LLToolBar::onClickRadar(void*)
{
	HBFloaterRadar::toggleInstance();
}

// static
void LLToolBar::onClickMap(void*)
{
	LLFloaterWorldMap::toggle(NULL);
}

// static
void LLToolBar::onClickRegionInfo(void*)
{
	// Mirror the existing World > Region Details location-privacy check.
	if (!gRLenabled || !gRLInterface.mContainsShowloc)
	{
		LLFloaterRegionInfo::showInstance();
	}
}

// static
void LLToolBar::onClickStatistics(void*)
{
	LLFloaterStats::toggleInstance();
}

void LLToolBar::onClickCameraControls(void*)
{
	LLFloaterCamera::toggleInstance();
}

// static
void LLToolBar::onClickFriends(void*)
{
	// The former Friends slot opens the existing native Teleport History floater.
	if (gFloaterTeleportHistoryp) gFloaterTeleportHistoryp->toggle();
}

// static
void LLToolBar::onClickAreaSearch(void*)
{
	// Native Cool VL Area Search: same floater as the enabled World menu item.
	HBFloaterAreaSearch::toggleInstance();
}

// static
void LLToolBar::onClickInventory(void*)
{
	handle_inventory(NULL);
}

// static
void LLToolBar::onClickFavorites(void*)
{
	HBFloaterWearableFavorites::toggleInstance();
}

// AO-only controls, connected to the existing Firestorm AO settings/editor.
// No other toolbar controls or AO engine implementation are modified.
void LLToolBar::onClickAOToggle(void*)
{
	gSavedPerAccountSettings.setBool("UseAO",
		!gSavedPerAccountSettings.getBool("UseAO"));
}

void LLToolBar::onClickAOEditor(void*)
{
	FloaterAO::showInstance();
}
