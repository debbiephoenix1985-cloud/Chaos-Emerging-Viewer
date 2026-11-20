 /**
 * @file llvoiceclient.cpp
 * @brief Implementation of LLVoiceClient class.
 *
 * $LicenseInfo:firstyear=2007&license=viewergpl$
 *
 * Copyright (c) 2007-2009, Linden Research, Inc.
 * Copyright (c) 2009-2024, Henri Beauchamp.
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

#include "llvoiceclient.h"

#include "llhttpnode.h"
#include "llkeyboard.h"
#include "llsdutil.h"

#include "llagent.h"
#include "llgridmanager.h"			// For gIsInSecondLife
#include "llmutelist.h"
#include "llviewercontrol.h"
#include "llviewerparcelmgr.h"
#include "llviewerregion.h"
#include "llvoicevivox.h"
#include "llvoicewebrtc.h"

const F32 LLVoiceClient::POWER_LEVEL_0 = OVERDRIVEN_POWER_LEVEL / 3.f;
const F32 LLVoiceClient::POWER_LEVEL_1 = OVERDRIVEN_POWER_LEVEL * 2.f / 3.f;
const F32 LLVoiceClient::POWER_LEVEL_2 = OVERDRIVEN_POWER_LEVEL;

// Global
LLVoiceClient gVoiceClient;

///////////////////////////////////////////////////////////////////////////////
// LLVoiceClientStatusObserver class
///////////////////////////////////////////////////////////////////////////////

std::string LLVoiceClientStatusObserver::status2string(LLVoiceClientStatusObserver::EStatusType status)
{
	std::string result = "UNKNOWN";

	// Prevent copy-paste errors when updating this list...
#define CASE(x)  case x:  result = #x;  break

	switch (status)
	{
		CASE(STATUS_LOGIN_RETRY);
		CASE(STATUS_LOGGED_IN);
		CASE(STATUS_JOINING);
		CASE(STATUS_JOINED);
		CASE(STATUS_LEFT_CHANNEL);
		CASE(STATUS_VOICE_DISABLED);
		CASE(STATUS_VOICE_ENABLED);
		CASE(BEGIN_ERROR_STATUS);
		CASE(ERROR_CHANNEL_FULL);
		CASE(ERROR_CHANNEL_LOCKED);
		CASE(ERROR_NOT_AVAILABLE);
		CASE(ERROR_UNKNOWN);

		default:
			break;
	}

#undef CASE

	return result;
}

///////////////////////////////////////////////////////////////////////////////
// LLVoiceClient class
///////////////////////////////////////////////////////////////////////////////

LLVoiceClient::LLVoiceClient()
:	mSpatialVoiceModulep(NULL),
	mNonSpatialVoiceModulep(NULL),
	mReady(false),
	mUserPTTState(false),
	mUsePTT(true),
	mPTTIsToggle(false),
	mMuteMic(false)
{
}

LLVoiceClient::~LLVoiceClient()
{
	terminate();	// Just in case it was forgotten... HB
}

void LLVoiceClient::init(LLPumpIO* pumpp)
{
	if (mReady)
	{
		return;
	}
	mReady = true;

	gVoiceVivox.init(pumpp);
	gVoiceWebRTC.init();
#if 0	// Do not do this here: wait for full login before enabling voice, else
		// race conditions would happen in the WebRTC connection process and
		// would result in failures to bring up voice on login. Instead, an
		// explicit call to LLVoiceClient::updateSettings() is now performed
		// from llstartup.cpp on STATE_CLEANUP stage. HB
	updateSettings();
#endif
	// Register or parcel manager observer for agent pracle changes. HB
	mParcelChangedConnection =
		gViewerParcelMgr.addAgentParcelChangedCB(std::bind(&LLVoiceClient::onParcelChange,
														   this));
}

void LLVoiceClient::terminate()
{
	if (mReady)
	{
		if (mParcelChangedConnection.connected())
		{
			mParcelChangedConnection.disconnect();
		}
		gVoiceVivox.terminate();
		gVoiceWebRTC.terminate();
		mReady = false;
	}
}

void LLVoiceClient::updateSettings()
{
	bool voice_enabled = gSavedSettings.getBool("EnableVoiceChat");
	bool ptt_enabled = gSavedSettings.getBool("PTTCurrentlyEnabled");
	if (!voice_enabled && !ptt_enabled)
	{
		// We must make sure PTT is enabled before shutting down voice. HB
		gSavedSettings.setBool("PTTCurrentlyEnabled", true);
		// The above will cause updateSettings() to be called again, so
		// do not continue at this point and rely on the new call. HB
		return;
	}
	setVoiceEnabled(voice_enabled);
	setUsePTT(ptt_enabled);
	if (!setPTTKey(gSavedSettings.getString("PushToTalkButton")))
	{
		llwarns << "Invalid push-to-talk key: trigger set to none" << llendl;
	}
	setPTTIsToggle(gSavedSettings.getBool("PushToTalkToggle"));
	setEarLocation(gSavedSettings.getS32("VoiceEarLocation"));
	setMicGain(gSavedSettings.getF32("AudioLevelMic"));
	setCaptureDevice(gSavedSettings.getString("VoiceInputAudioDevice"), false);
	setRenderDevice(gSavedSettings.getString("VoiceOutputAudioDevice"), false);
	setCaptureDevice(gSavedSettings.getString("VoiceWebRTCInputAudioDevice"),
					 true);
	setRenderDevice(gSavedSettings.getString("VoiceWebRTCOutputAudioDevice"),
					 true);
	gVoiceWebRTC.updateSettings();
	updateMicMuteLogic();
}

LLVoiceModule* LLVoiceClient::getModuleFromType(const std::string& server_type)
{
	if (server_type.empty())
	{
		// Note: Vivox was removed from SL, so an empty server type now means
		// "webrtc" in SL, "vivox" in OpenSim. HB
		return gIsInSecondLife ? (LLVoiceModule*)&gVoiceWebRTC 
							   : (LLVoiceModule*)&gVoiceVivox;
	}
	if (server_type == "vivox")
	{
		return &gVoiceVivox;
	}
	if (server_type == "webrtc")
	{
		return &gVoiceWebRTC;
	}
	return NULL;
}

LLVoiceModule* LLVoiceClient::getModuleFromChannelInfo(const LLSD& info)
{
	if (info.has("voice_server_type"))
	{
		return getModuleFromType(info["voice_server_type"].asString());
	}
	// An invitation without voice_server_type uses the active region's
	// advertised provider, retained in VoiceServerType by
	// getModuleFromSimFeatures(). This is essential for OpenSim WebRTC P2P.
	return gSavedSettings.getString("VoiceServerType") == "webrtc" ?
		   (LLVoiceModule*)&gVoiceWebRTC : (LLVoiceModule*)&gVoiceVivox;
}

U32 LLVoiceClient::getVoiceServerType(const LLSD& channel_info)
{
	if (!channel_info.has("voice_server_type"))
	{
		// The agent region advertises its voice provider via
		// getModuleFromSimFeatures(), including OpenSim WebRTC regions.
		return gSavedSettings.getString("VoiceServerType") == "webrtc" ?
			WEBRTC_SERVER : VIVOX_SERVER;
	}
	std::string type = channel_info["voice_server_type"].asString();
	if (type == "vivox")
	{
		return VIVOX_SERVER;
	}
	if (type == "webrtc")
	{
		return WEBRTC_SERVER;
	}
	return UNKNOWN_SERVER;
}

//static
LLVoiceModule* LLVoiceClient::getModuleFromSimFeatures(const LLSD& features,
													   bool save_to_settings)
{
	// Default is Vivox for OpenSim and WebRTC for SL when no voice type
	// feature exists (old sim versions, race conditions).
	LLVoiceModule* modulep = gIsInSecondLife ? (LLVoiceModule*)&gVoiceWebRTC
											 : (LLVoiceModule*)&gVoiceVivox;
	std::string type_str;
	if (features.has("VoiceServerType"))
	{
		type_str = features["VoiceServerType"].asString();
		if (type_str == "webrtc")
		{
			modulep = &gVoiceWebRTC;
		}
		else if (type_str == "vivox")
		{
			modulep = &gVoiceVivox;
		}
		else
		{
			modulep = NULL;
			type_str = "UNSUPPORTED";
		}
	}
	if (save_to_settings)
	{
		if (type_str.empty())
		{
			// Note: Vivox was removed from SL, so an empty server type now means
			// "webrtc" in SL, "vivox" in OpenSim. HB
			type_str = gIsInSecondLife ? "webrtc" : "vivox";
			llinfos << " No voice server type given for agent region: "
					<< type_str << " assumed." << llendl;
		}
		else
		{
			llinfos << "Voice server type set for agent region: " << type_str
					<< llendl;
		}
		gSavedSettings.setString("VoiceServerType", type_str);
	}
	return modulep;
}

void LLVoiceClient::setSpatialVoiceModule(LLVoiceModule* modulep)
{
	if (!modulep || modulep == mSpatialVoiceModulep)
	{
		return;
	}

	bool proximal_active = mSpatialVoiceModulep &&
						   mSpatialVoiceModulep->inProximalChannel();
	if (proximal_active)
	{
		mSpatialVoiceModulep->processChannels(false);
	}
	mSpatialVoiceModulep = modulep;
	if (proximal_active)
	{
		mSpatialVoiceModulep->processChannels(true);
	}
	// Firestorm refreshes the selected spatial voice provider after switching.
	if (mSpatialVoiceModulep == &gVoiceWebRTC)
	{
		gVoiceWebRTC.updateSettings();
	}
	LL_DEBUGS("Voice") << "Spatial voice module changed for: "
					   << mSpatialVoiceModulep->getName() << LL_ENDL;
}

void LLVoiceClient::setNonSpatialVoiceModule(LLVoiceModule* modulep)
{
	mNonSpatialVoiceModulep = modulep;
	// Firestorm refreshes the selected provider before starting a new
	// non-spatial call. In Chaos only WebRTC exposes updateSettings().
	if (mNonSpatialVoiceModulep == &gVoiceWebRTC)
	{
		gVoiceWebRTC.updateSettings();
	}
	// If do not have a non-spatial module, revert to spatial when possible.
	if (!mNonSpatialVoiceModulep && mSpatialVoiceModulep)
	{
		mSpatialVoiceModulep->processChannels(true);
	}
}

void LLVoiceClient::handleSimFeaturesReceived(const LLSD& features)
{
	LL_DEBUGS("Voice") << "Processing simulator features for agent region"
					   << LL_ENDL;
	LLVoiceModule* modulep = getModuleFromSimFeatures(features, true);
	if (!modulep)
	{
		return;	// Unknown server type: nothing to do !
	}
	if (mSpatialVoiceModulep && !mNonSpatialVoiceModulep)
	{
		// Stop processing if we are going to change voice clients and we are
		// not currently in non-spatial.
		if (mSpatialVoiceModulep != modulep)
		{
			llinfos << "Disabling " << mSpatialVoiceModulep->getName()
					<< " voice processing." << llendl;
			mSpatialVoiceModulep->processChannels(false);
		}
	}
	// Switch to spatial voice to the new module if needed.
	setSpatialVoiceModule(modulep);
		
	// If we should be in spatial voice, switch to it and set the credentials
	if (mSpatialVoiceModulep && !mNonSpatialVoiceModulep)
	{
		LL_DEBUGS("Voice") << "Using spatial voice module." << LL_ENDL;
		if (!mSpatialCredentials.isUndefined())
		{
			mSpatialVoiceModulep->setSpatialChannel(mSpatialCredentials);
		}
		mSpatialVoiceModulep->processChannels(true);
	}
}

void LLVoiceClient::onParcelChange()
{
	LL_DEBUGS("Voice") << "Parcel change detected." << LL_ENDL;
	if (!mSpatialVoiceModulep || mNonSpatialVoiceModulep)
	{
		// Not in parcel/estate voice channel currently. HB
		return;
	}
	// Check for parcel voice permissions and act accordingly. HB
	if (gViewerParcelMgr.allowAgentVoice())
	{
		LL_DEBUGS("Voice") << "Parcel voice allowed." << LL_ENDL;
		mSpatialVoiceModulep->processChannels(true);
		setSpatialChannel(mSpatialCredentials);
		if (mSpatialVoiceModulep->isVivox())
		{
			gVoiceVivox.parcelChanged();
		}
		return;
	}
	// Parcel flag does not permit voice: make sure the spatial channel will
	// be shut down and will stay as such. HB
	if (mSpatialVoiceModulep && mSpatialVoiceModulep->inProximalChannel())
	{
		LL_DEBUGS("Voice") << "Parcel voice disabled. Switching off spatial voice."
						   << LL_ENDL;
		mSpatialVoiceModulep->processChannels(false);
	}
}

void LLVoiceClient::setVoiceEnabled(bool enabled)
{
	gVoiceVivox.setVoiceEnabled(enabled);
	gVoiceWebRTC.setVoiceEnabled(enabled);
}

void LLVoiceClient::userAuthorized(const std::string& first_name,
								   const std::string& last_name,
								   const LLUUID& agent_id)
{
	gVoiceVivox.userAuthorized(first_name, last_name, agent_id);
	// Kick up a parcel change since the one which occurred at login was done
	// too soon and got ignored. HB
	onParcelChange();
	// Note: no such method for LLVoiceWebRTC
}

bool LLVoiceClient::isCaptureNoDevice(bool webrtc)
{
	// Note: Vivox got its own "No Device" entry listed.
	return webrtc && gVoiceWebRTC.isCaptureNoDevice();
}

bool LLVoiceClient::isRenderNoDevice(bool webrtc)
{
	// Note: Vivox got its own "No Device" entry listed.
	return webrtc && gVoiceWebRTC.isRenderNoDevice();
}

void LLVoiceClient::setCaptureDevice(const std::string& device_id, bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.setCaptureDevice(device_id);
	}
	else
	{
		gVoiceVivox.setCaptureDevice(device_id);
	}
}

void LLVoiceClient::setRenderDevice(const std::string& device_id, bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.setRenderDevice(device_id);
	}
	else
	{
		gVoiceVivox.setRenderDevice(device_id);
	}
}

const strings_map_t& LLVoiceClient::getCaptureDevices(bool webrtc) const
{
	if (webrtc)
	{
		return gVoiceWebRTC.getCaptureDevices();
	}
	return gVoiceVivox.getCaptureDevices();
}

const strings_map_t& LLVoiceClient::getRenderDevices(bool webrtc) const
{
	if (webrtc)
	{
		return gVoiceWebRTC.getRenderDevices();
	}
	return gVoiceVivox.getRenderDevices();
}

void LLVoiceClient::tuningStart(bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.setTuningMode(true);
	}
	else
	{
		gVoiceVivox.setTuningMode(true);
	}
}

void LLVoiceClient::tuningStop(bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.setTuningMode(false);
	}
	else
	{
		gVoiceVivox.setTuningMode(false);
	}
}

bool LLVoiceClient::inTuningMode(bool webrtc)
{
	if (webrtc)
	{
		return gVoiceWebRTC.inTuningMode();
	}
	return gVoiceVivox.inTuningMode();
}

bool LLVoiceClient::tuningModeActive()
{
	static LLCachedControl<std::string> enable_voice(gSavedSettings,
													 "VoiceServerType");
	if (enable_voice() == "webrtc")
	{
		return gVoiceWebRTC.inTuningMode();
	}
	return gVoiceVivox.inTuningMode();
}

void LLVoiceClient::tuningSetMicVolume(F32 volume, bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.tuningSetMicVolume(volume);
	}
	else
	{
		gVoiceVivox.tuningSetMicVolume(volume);
	}
}

#if 0	// Not used
void LLVoiceClient::tuningSetSpeakerVolume(F32 volume, bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.tuningSetSpeakerVolume(volume);
	}
	else
	{
		gVoiceVivox.tuningSetSpeakerVolume(volume);
	}
}
#endif

F32 LLVoiceClient::tuningGetEnergy(bool webrtc)
{
	if (webrtc)
	{
		return gVoiceWebRTC.tuningGetEnergy();
	}
	return gVoiceVivox.tuningGetEnergy();
}

bool LLVoiceClient::deviceSettingsAvailable(bool webrtc)
{
	if (webrtc)
	{
		return gVoiceWebRTC.deviceSettingsAvailable();
	}
	return gVoiceVivox.deviceSettingsAvailable();
}

void LLVoiceClient::refreshDeviceLists(bool clear_current_list, bool webrtc)
{
	if (webrtc)
	{
		gVoiceWebRTC.refreshDeviceLists(clear_current_list);
	}
	else
	{
		gVoiceVivox.refreshDeviceLists(clear_current_list);
	}
}

// This is only ever used to answer incoming P2P call invites.
bool LLVoiceClient::answerInvite(const LLSD& channel_info)
{
	// Note: no such method for LLVoiceWebRTC
	std::string handle = channel_info["session_handle"].asString();
	return gVoiceVivox.answerInvite(handle);
}

void LLVoiceClient::declineInvite(const LLSD& channel_info)
{
	// Note: no such method for LLVoiceWebRTC
	std::string handle = channel_info["session_handle"].asString();
	gVoiceVivox.declineInvite(handle);
}

bool LLVoiceClient::getParticipants(participants_vec_t& participants)
{
	participants.clear();
	bool result = false;

	LLVoiceVivox::particip_map_t* listp = gVoiceVivox.getParticipantList();
	if (listp)
	{
		result = true;
		for (LLVoiceVivox::particip_map_t::const_iterator it = listp->begin(),
														  end = listp->end();
			 it != end; ++it)
		{
			LLVoiceVivox::participantState* participantp = it->second;
			participants.emplace_back(participantp->mAvatarID,
									  participantp->mLegacyName,
									  participantp->isAvatar());
		}
	}

	LLVoiceWebRTC::particip_map_t* list2p = gVoiceWebRTC.getParticipantList();
	if (list2p)
	{
		result = true;
		for (LLVoiceWebRTC::particip_map_t::const_iterator it = list2p->begin(),
														   end = list2p->end();
			 it != end; ++it)
		{
			LLVoiceWebRTC::pstate_ptr_t participantp = it->second;
			participants.emplace_back(participantp->mAvatarID,
									  participantp->mLegacyName, true);
		}
	}

	return result;
}

void LLVoiceClient::addObserver(LLVoiceClientStatusObserver* observerp)
{
	gVoiceVivox.addObserver(observerp);
	gVoiceWebRTC.addObserver(observerp);
}

void LLVoiceClient::removeObserver(LLVoiceClientStatusObserver* observerp)
{
	gVoiceVivox.removeObserver(observerp);
	gVoiceWebRTC.removeObserver(observerp);
}

void LLVoiceClient::setSpatialChannel(const LLSD& channel_info)
{
	mSpatialCredentials = channel_info;

	LLViewerRegion* regionp = gAgent.getRegion();
	if (!regionp || !regionp->getFeaturesReceived())
	{
		return;
	}

	const LLSD& features = regionp->getSimulatorFeatures();
	setSpatialVoiceModule(getModuleFromSimFeatures(features));

	if (mSpatialVoiceModulep)
	{
		mSpatialVoiceModulep->setSpatialChannel(channel_info);
	}
}

void LLVoiceClient::setNonSpatialChannel(const LLSD& channel_info,
										 bool notify_on_first_join,
										 bool hangup_on_last_leave)
{
	LLVoiceModule* modulep = getModuleFromChannelInfo(channel_info);
	setNonSpatialVoiceModule(modulep);

	if (mSpatialVoiceModulep &&
		mSpatialVoiceModulep != mNonSpatialVoiceModulep)
	{
		mSpatialVoiceModulep->processChannels(false);
	}
	if (mNonSpatialVoiceModulep)
	{
		mNonSpatialVoiceModulep->processChannels(true);
		mNonSpatialVoiceModulep->setNonSpatialChannel(channel_info,
													  notify_on_first_join,
													  hangup_on_last_leave);
	}
}

void LLVoiceClient::leaveNonSpatialChannel()
{
	if (mNonSpatialVoiceModulep)
	{
		mNonSpatialVoiceModulep->leaveNonSpatialChannel();
		mNonSpatialVoiceModulep->processChannels(false);
		mNonSpatialVoiceModulep = NULL;
	}
}

void LLVoiceClient::activateSpatialChannel(bool activate)
{
	if (mSpatialVoiceModulep)
	{
		if (activate && !gViewerParcelMgr.allowAgentVoice())
		{
			LL_DEBUGS("Voice") << "Not activating due to parcel no-voice flag"
							   << LL_ENDL;
			activate = false;
		}
		mSpatialVoiceModulep->processChannels(activate);
	}
}

bool LLVoiceClient::isCurrentChannel(const LLSD& channel_info)
{
	return gVoiceVivox.isCurrentChannel(channel_info) ||
		   gVoiceWebRTC.isCurrentChannel(channel_info);
}

bool LLVoiceClient::compareChannels(const LLSD& info1, const LLSD& info2)
{
	return gVoiceVivox.compareChannels(info1, info2) ||
		   gVoiceWebRTC.compareChannels(info1, info2);
}

bool LLVoiceClient::inProximalChannel() const
{
	return mSpatialVoiceModulep && mSpatialVoiceModulep->inProximalChannel();
}

void LLVoiceClient::callUser(const LLUUID& id, U32 server_type)
{
	// Note: there is no callUser() equivalent for WebRTC
	if (server_type == VIVOX_SERVER)
	{
		LL_DEBUGS("Voice") << "Calling user " << id << " via Viviox."
						   << LL_ENDL;
		gVoiceVivox.callUser(id);
	}
}

void LLVoiceClient::hangup(U32 server_type)
{
	// WebRTC ad-hoc calls are ended by LLVoiceChannelGroup::deactivate().
	// Only legacy Vivox calls use the P2P hangup interface.
	if (server_type == VIVOX_SERVER)
	{
		LL_DEBUGS("Voice") << "Leaving Vivox session" << LL_ENDL;
		// Note: in LL's viewer code, LLVoiceVivox::hangup() is an alias to
		// LLVoiceVivox::leaveChannel(). HB
		gVoiceVivox.leaveChannel();
	}
}

std::string LLVoiceClient::sipURIFromID(const LLUUID& id) const
{
	if (mNonSpatialVoiceModulep)
	{
		return mNonSpatialVoiceModulep->sipURIFromID(id);
	}
	if (mSpatialVoiceModulep)
	{
		return mSpatialVoiceModulep->sipURIFromID(id);
	}
	return "";
}

bool LLVoiceClient::isVoiceWorking()
{
	return voiceEnabled() &&
		   (gVoiceVivox.isVoiceWorking() || gVoiceWebRTC.isVoiceWorking());
}

bool LLVoiceClient::voiceEnabled()
{
	static LLCachedControl<bool> enable_voice(gSavedSettings,
											  "EnableVoiceChat");
	static LLCachedControl<bool> disable_voice(gSavedSettings,
											   "CmdLineDisableVoice");
	return enable_voice && !disable_voice;
}

bool LLVoiceClient::getVoiceEnabled(const LLUUID& id)
{
	return gVoiceVivox.isParticipant(id) || gVoiceWebRTC.isParticipant(id);
}

bool LLVoiceClient::getIsSpeaking(const LLUUID& id)
{
	if (id == gAgentID)
	{
		return isAgentMicOpen();
	}
	return gVoiceVivox.getIsSpeaking(id) || gVoiceWebRTC.getIsSpeaking(id);
}

bool LLVoiceClient::getIsModeratorMuted(const LLUUID& id)
{
	return gVoiceVivox.getIsModeratorMuted(id) ||
		   gVoiceWebRTC.getIsModeratorMuted(id);
}

EVoicePowerLevel LLVoiceClient::getPowerLevel(const LLUUID& id)
{
	F32 power = getCurrentPower(id);
	if (getOnMuteList(id))
	{
		return VPL_MUTED;
	}
	else if (power == 0.f && !getIsSpeaking(id))
	{
		return VPL_PTT_Off;
	}
	else if (power < POWER_LEVEL_0)
	{
		return VPL_PTT_On;
	}
	else if (power < POWER_LEVEL_1)
	{
		return VPL_Level1;
	}
	else if (power < POWER_LEVEL_2)
	{
		return VPL_Level2;
	}
	else
	{
		return VPL_Level3;
	}
}
F32 LLVoiceClient::getCurrentPower(const LLUUID& id)
{
	F32 power = gVoiceWebRTC.getCurrentPower(id);
	if (power < 0.f)	// 'id' is not in a WebRTC session
	{
		// Try Vivox then...
		power = gVoiceVivox.getCurrentPower(id);
	}
	return llmax(power, 0.f);
}

void LLVoiceClient::setEarLocation(S32 loc)
{
	gVoiceVivox.setEarLocation(loc);
	gVoiceWebRTC.setEarLocation(loc);
}

void LLVoiceClient::setVoiceVolume(F32 volume)
{
	gVoiceVivox.setVoiceVolume(volume);
	gVoiceWebRTC.setVoiceVolume(volume);
}

void LLVoiceClient::setMicGain(F32 volume)
{
	gVoiceVivox.setMicGain(volume);
	gVoiceWebRTC.setMicGain(volume);
}

F32 LLVoiceClient::getUserVolume(const LLUUID& id)
{
	F32 volume = gVoiceVivox.getUserVolume(id);
	if (volume < 0.f)	// 'id' is not in a Vivox session
	{
		volume = gVoiceWebRTC.getUserVolume(id);
	}
	return llmax(volume, 0.f);
}

void LLVoiceClient::setUserVolume(const LLUUID& id, F32 volume)
{
	volume = llclamp(volume, 0.f, 1.f);
	gVoiceVivox.setUserVolume(id, volume);
	gVoiceWebRTC.setUserVolume(id, volume);
}

bool LLVoiceClient::getOnMuteList(const LLUUID& id)
{
	return LLMuteList::isMuted(id, LLMute::flagVoiceChat);
}

// PTT related methods

bool LLVoiceClient::isAgentMicOpen() const
{
	// Not in push to talk mode, or push to talk is active means currently
	// talking.
	static LLCachedControl<bool> ptt_enabled(gSavedSettings,
											 "PTTCurrentlyEnabled");
	return mUserPTTState || !ptt_enabled;
}

void LLVoiceClient::updateMicMuteLogic()
{
	// If not configured to use PTT, the mic should be open (otherwise the user
	// would be unable to speak).
	bool new_mic_mute = false;
	if (mUsePTT)
	{
		new_mic_mute = !mUserPTTState;
	}
	if (mMuteMic)
	{
		// This always overrides any other PTT setting.
		new_mic_mute = true;
	}
	gVoiceWebRTC.setMuteMic(new_mic_mute);
}

void LLVoiceClient::setMuteMic(bool muted)
{
	if (mMuteMic != muted)
	{
		mMuteMic = muted;
		updateMicMuteLogic();
	}
}

void LLVoiceClient::setMutedInfo(const std::string& channel_id, bool muted)
{
	mute_map_t::iterator it = mChannelMuteMap.find(channel_id);
	if (it == mChannelMuteMap.end())
	{
		if (muted && inProximalChannel())
		{
			// Channel is new and being muted: notify
			gNotifications.add("NearbyVoiceMutedByModerator");
		}
		mChannelMuteMap[channel_id] = muted;
	}
	else if (it->second != muted)
	{
		// Channel mute status already known: notify about the change
		gNotifications.add(muted ? "NearbyVoiceMutedByModerator"
								 : "NearbyVoiceUnmutedByModerator");
		it->second = muted;
	}
	if (muted && mUserPTTState)
	{
		setUserPTTState(false);
	}
}

void LLVoiceClient::setUserPTTState(bool ptt)
{
	if (ptt)
	{
		// If nearby chat is muted by moderator, do not toggle PTT and notify
		// the user.
		if (!mUserPTTState && getIsModeratorMuted(gAgentID))
		{
			gNotifications.add("NearbyVoiceMutedByModerator");
			return;
		}
	}
	mUserPTTState = ptt;
	updateMicMuteLogic();
}

void LLVoiceClient::setUsePTT(bool use_it)
{
	if (use_it && !mUsePTT)
	{
		// When the user turns on PTT, reset the current state.
		mUserPTTState = false;
	}
	mUsePTT = use_it;
	updateMicMuteLogic();
}

void LLVoiceClient::setPTTIsToggle(bool set_as_toggle)
{
	if (!set_as_toggle && mPTTIsToggle)
	{
		// When the user turns off toggle, reset the current state.
		mUserPTTState = false;
	}
	mPTTIsToggle = set_as_toggle;
	updateMicMuteLogic();
}

bool LLVoiceClient::setPTTKey(const std::string& key)
{
	if (key == "MiddleMouse")
	{
		mPTTIsMiddleMouse = true;
		return true;
	}
	mPTTIsMiddleMouse = false;
	return LLKeyboard::keyFromString(key.c_str(), &mPTTKey);
}

void LLVoiceClient::keyDown(KEY key, MASK mask)
{
	if (!gKeyboardp || gKeyboardp->getKeyRepeated(key))
	{
		return;	// Ignore auto-repeat keys
	}

	if (!mPTTIsMiddleMouse)
	{
		if (mPTTIsToggle)
		{
			if (key == mPTTKey)
			{
				toggleUserPTTState();
			}
		}
		else if (mPTTKey != KEY_NONE)
		{
			setUserPTTState(gKeyboardp->getKeyDown(mPTTKey));
		}
	}
}

void LLVoiceClient::keyUp(KEY key, MASK mask)
{
	if (!mPTTIsMiddleMouse)
	{
		if (!mPTTIsToggle && (mPTTKey != KEY_NONE) && gKeyboardp)
		{
			setUserPTTState(gKeyboardp->getKeyDown(mPTTKey));
		}
	}
}

void LLVoiceClient::inputUserControlState(bool down)
{
	if (!mPTTIsToggle)
	{
		// Set open-mic state as an absolute
		setUserPTTState(down);
	}
	else if (down)	// Toggle open-mic state on 'down'
	{
		toggleUserPTTState();
	}
}

void LLVoiceClient::middleMouseState(bool down)
{
	if (mPTTIsMiddleMouse)
	{
		inputUserControlState(down);
	}
}

///////////////////////////////////////////////////////////////////////////////
// LLViewerParcelVoiceInfo class
///////////////////////////////////////////////////////////////////////////////

class LLViewerParcelVoiceInfo final : public LLHTTPNode
{
	void post(LLHTTPNode::ResponsePtr response, const LLSD& context,
			  const LLSD& input) const override
	{
		// The parcel you are in has changed something about its voice
		// information. This is a misnomer, as it can also be when you are not
		// in a parcel at all. Should really be something like
		// LLViewerVoiceInfoChanged...
		if (input.has("body"))
		{
			const LLSD& body = input["body"];

			// body has "region_name" (str), "parcel_local_id"(int),
			// "voice_credentials" (map).

			// body["voice_credentials"] has "channel_uri" (str),
			// body["voice_credentials"] has "channel_credentials" (str)

			// If we really wanted to be extra careful, we would check the
			// supplied local parcel id to make sure it is for the same
			// parcel we believe we are in.
			if (body.has("voice_credentials"))
			{
				LL_DEBUGS("Voice") << "Got spatial voice channel info"
								   << LL_ENDL;
				gVoiceClient.setSpatialChannel(body["voice_credentials"]);
			}
		}
	}
};

LLHTTPRegistration<LLViewerParcelVoiceInfo>
    gHTTPRegistrationMessageParcelVoiceInfo("/message/ParcelVoiceInfo");

///////////////////////////////////////////////////////////////////////////////
// LLViewerRequiredVoiceVersion class
///////////////////////////////////////////////////////////////////////////////

class LLViewerRequiredVoiceVersion final : public LLHTTPNode
{
	void post(LLHTTPNode::ResponsePtr response, const LLSD& context,
			  const LLSD& input) const override
	{
		// You received this messsage (most likely on region cross or teleport)
		if (!gVoiceClient.ready() || !input.has("body"))
		{
			return;
		}
		LLViewerRegion* regionp = gAgent.getRegion();
		if (!regionp || !regionp->getFeaturesReceived())
		{
			return;
		}
		const LLSD& body = input["body"];
		if (!body.has("major_version"))
		{
			return;
		}
		LL_DEBUGS("Voice") << "Got Voice version info: "
						   << ll_pretty_print_sd(body) << LL_ENDL;
		// Check for the voice server version, based on its type.
		LLVoiceModule* modulep = gVoiceClient.getModuleFromChannelInfo(body);
		// Default to -1 to cause failure for unknown server type. HB
		S32 max_version = -1;
		if (modulep && modulep->isVivox())
		{
			constexpr S32 VIVOX_MAJOR_VERSION = 1;
			max_version = VIVOX_MAJOR_VERSION;
		}
		else if (modulep && modulep->isWebRTC())
		{
			constexpr S32 WEBRTC_MAJOR_VERSION = 2;
			max_version = WEBRTC_MAJOR_VERSION;
		}
		if (body["major_version"].asInteger() > max_version)
		{
			gNotifications.add("VoiceVersionMismatch");
			// Toggles the listener
			gSavedSettings.setBool("EnableVoiceChat", false);
		}
	}
};

LLHTTPRegistration<LLViewerRequiredVoiceVersion>
    gHTTPRegistrationMessageRequiredVoiceVersion("/message/RequiredVoiceVersion");
