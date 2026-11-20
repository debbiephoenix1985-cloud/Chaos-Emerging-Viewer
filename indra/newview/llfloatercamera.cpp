/**
 * @file llfloatercamera.cpp
 * @brief Container for camera control buttons (zoom, pan, orbit)
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

#include "llfloatercamera.h"

#include "llbutton.h"
#include "llcombobox.h"
#include "llfloaterpreference.h"
#include "llselectmgr.h"
#include "llspinctrl.h"
#include "lltoolmgr.h"
#include "llviewerobject.h"
#include "lluictrlfactory.h"

#include "llagent.h"
#include "lljoystickbutton.h"
#include "llviewercontrol.h"
#include "llviewermenu.h"

// Constants
constexpr F32 CAMERA_BUTTON_DELAY = 0.0f;

//
// Member functions
//

LLFloaterCamera::LLFloaterCamera(const LLSD& val)
:	LLFloater("camera controls") // Movable Firestorm-style camera floater
{
	// Keep the normal floater drag handle and resize grips active.
	setIsChrome(false);

	// For now, only used for size and tooltip strings
	LLUICtrlFactory::getInstance()->buildFloater(this, "floater_camera.xml",
												 NULL, false); // Do not open
}

//virtual
bool LLFloaterCamera::postBuild()
{
	// The 19px toolbar ends at top-40, and the Position row begins
	// at top-104. Keep the 48px pad strictly between them, with 8px
	// clearance above and below. Its hitbox equals its drawn bounds.
	S32 top = getRect().getHeight() - 48;
	S32 bottom = top - 48;
	S32 left = 4;

	constexpr S32 ROTATE_WIDTH = 48;
	mRotate = new LLJoystickCameraRotate("cam rotate stick",
										 LLRect(left, top,
												left + ROTATE_WIDTH, bottom),
										 "Chaos_Cam_Rotate_Out.png",
										 "Chaos_Cam_Rotate_In.png");
	mRotate->setFollows(FOLLOWS_TOP | FOLLOWS_LEFT);
	mRotate->setScaleImage(true);
	mRotate->setHeldDownDelay(CAMERA_BUTTON_DELAY);
	mRotate->setToolTip(getString("rotate_tooltip"));
	mRotate->setSoundFlags(MOUSE_DOWN | MOUSE_UP);
	addChild(mRotate);

	left += ROTATE_WIDTH + 20;

	constexpr S32 ZOOM_WIDTH = 16;
	mZoom = new LLJoystickCameraZoom("zoom",
									 LLRect(left, top,
											left + ZOOM_WIDTH, bottom),
									 "cam_zoom_out.tga",
									 "cam_zoom_plus_in.tga",
									 "cam_zoom_minus_in.tga");
	mZoom->setFollows(FOLLOWS_TOP | FOLLOWS_LEFT);
	mZoom->setScaleImage(true);
	mZoom->setHeldDownDelay(CAMERA_BUTTON_DELAY);
	mZoom->setToolTip(getString("zoom_tooltip"));
	mZoom->setSoundFlags(MOUSE_DOWN | MOUSE_UP);
	addChild(mZoom);

	left += ZOOM_WIDTH + 20;

	constexpr S32 TRACK_WIDTH = 48;
	mTrack = new LLJoystickCameraTrack("cam track stick",
									   LLRect(left, top, left + TRACK_WIDTH, bottom),
									   "Chaos_Cam_Tracking_Out.png",
									   "Chaos_Cam_Tracking_In.png");
	mTrack->setFollows(FOLLOWS_TOP | FOLLOWS_RIGHT);
	mTrack->setScaleImage(true);
	mTrack->setHeldDownDelay(CAMERA_BUTTON_DELAY);
	mTrack->setToolTip(getString("move_tooltip"));
	mTrack->setSoundFlags(MOUSE_DOWN | MOUSE_UP);
	addChild(mTrack);

	getChild<LLButton>("view_front")->setClickedCallback(onClickFront, this);
	getChild<LLButton>("view_side")->setClickedCallback(onClickSide, this);
	getChild<LLButton>("view_rear")->setClickedCallback(onClickRear, this);
	getChild<LLButton>("view_tpp")->setClickedCallback(onClickTPP, this);
	getChild<LLButton>("view_object")->setClickedCallback(onClickObject, this);
	getChild<LLButton>("view_mouselook")->setClickedCallback(onClickMouselook, this);
	getChild<LLButton>("reset_camera")->setClickedCallback(onClickResetCamera, this);
	getChild<LLButton>("camera_position_btn")->setClickedCallback(onClickPosition, this);
	getChild<LLButton>("gear_btn")->setClickedCallback(onClickPosition, this);
	getChild<LLButton>("save_preset_btn")->setClickedCallback(onClickSave, this);
	getChild<LLComboBox>("preset_combo")->setCommitCallback(onSelectPreset);
	getChild<LLComboBox>("preset_combo")->setCallbackUserData(this);

	return true;
}

void LLFloaterCamera::onClickResetCamera(void* userdata)
{
	// Route through the existing reset action so avatar customization and
	// scripted camera restrictions keep their normal safeguards.
	handle_reset_view();
}

//virtual
void LLFloaterCamera::onOpen()
{
	LLFloater::onOpen();
	gSavedSettings.setBool("ShowCameraControls", true);
}

//virtual
void LLFloaterCamera::onClose(bool app_quitting)
{
	LLFloater::onClose(app_quitting);
	if (!app_quitting)
	{
		gSavedSettings.setBool("ShowCameraControls", false);
	}
}

//virtual
void LLFloaterCamera::draw()
{
	LLFloater::draw();
}

// Keep view changes on the existing OpenSim/Cool VL camera pipeline.
void LLFloaterCamera::onClickFront(void*)
{
    handle_reset_view();
    gSavedSettings.setBool("CameraFrontView", true);
}

void LLFloaterCamera::onClickSide(void*)
{
    gSavedSettings.setBool("CameraFrontView", false);
    handle_reset_view();
    gAgent.cameraOrbitAround(F_PI * 0.5f);
}

void LLFloaterCamera::onClickRear(void*)
{
    gSavedSettings.setBool("CameraFrontView", false);
    handle_reset_view();
}

void LLFloaterCamera::onClickTPP(void*)
{
    gSavedSettings.setBool("CameraFrontView", false);
    handle_reset_view();
}

void LLFloaterCamera::onClickObject(void*)
{
    // Focus the selected object, or enable click-to-focus for the next object.
    LLViewerObject* object = gSelectMgr.getSelection()->getPrimaryObject();
    if (object)
    {
        gAgent.setFocusGlobal(object->getPositionGlobal(), object->getID());
    }
    else
    {
        gToolMgr.setCurrentToolset(gCameraToolset);
    }
}
void LLFloaterCamera::onClickMouselook(void*)
{
    gAgent.changeCameraToMouselook();
}

void LLFloaterCamera::onClickPosition(void*)
{
    // The legacy viewer's Input & Camera preferences contain the camera
    // offset controls. Do not open a non-existent Firestorm-only floater.
    LLFloaterPreference::openInTab(1);
}

void LLFloaterCamera::onClickSave(void*)
{
    // Persist a custom camera view relative to the avatar, so teleporting
    // and relogging do not leave it anchored in old region coordinates.
    LLCoordFrame frame = gAgent.getFrameAgent();
    LLVector3d avatar = gAgent.getPositionGlobal();
    LLVector3d pos = gAgent.getCameraPositionGlobal() - avatar;
    LLVector3d focus = gAgent.getFocusGlobal() - avatar;
    LLVector3 cam_local = frame.rotateToLocal(LLVector3((F32)pos.mdV[VX], (F32)pos.mdV[VY], (F32)pos.mdV[VZ]));
    LLVector3 focus_local = frame.rotateToLocal(LLVector3((F32)focus.mdV[VX], (F32)focus.mdV[VY], (F32)focus.mdV[VZ]));
    gSavedSettings.setVector3("ChaosCameraMyViewPos", cam_local);
    gSavedSettings.setVector3("ChaosCameraMyViewFocus", focus_local);
    gSavedSettings.setBool("ChaosCameraMyViewSaved", true);
}

void LLFloaterCamera::onSelectPreset(LLUICtrl* ctrl, void*)
{
    if (ctrl)
    {
        selectPreset(ctrl->getValue().asString());
    }
}
void LLFloaterCamera::selectPreset(const std::string& preset)
{
    if (preset == "Front View" || preset == "front")
    {
        onClickFront(NULL);
    }
    else if (preset == "Side View" || preset == "side")
    {
        onClickSide(NULL);
    }
    else if (preset == "Rear View" || preset == "rear")
    {
        onClickRear(NULL);
    }
    else if ((preset == "My View" || preset == "myview") &&
             gSavedSettings.getBool("ChaosCameraMyViewSaved"))
    {
        handle_reset_view();
        const LLCoordFrame frame = gAgent.getFrameAgent();
        const LLVector3d avatar = gAgent.getPositionGlobal();
        const LLVector3 pos = frame.rotateToAbsolute(gSavedSettings.getVector3("ChaosCameraMyViewPos"));
        const LLVector3 focus = frame.rotateToAbsolute(gSavedSettings.getVector3("ChaosCameraMyViewFocus"));
        gAgent.setCameraPosAndFocusGlobal(avatar + LLVector3d(pos),
                                         avatar + LLVector3d(focus), LLUUID::null);
    }
}

