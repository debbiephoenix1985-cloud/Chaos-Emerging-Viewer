/**
*  @file llvoicevivox.h
 * @brief Declaration of LLVoiceVivox class which is the interface to the
 * Vivox voice client process.
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

#pragma once

#include "llhost.h"
#include "lliosocket.h"

#include "llvoiceclient.h"

class LLFrameTimer;
class LLProcessLauncher;
class LLPumpIO;
class LLTimer;
class LLVOAvatar;
struct XML_ParserStruct;

class LLVoiceVivox final : public LLVoiceModule
{
	friend class LLVivoxProtocolParser;
	friend class LLVoiceClient;

protected:
	LOG_CLASS(LLVoiceVivox);

public:
	LLVoiceVivox();
	~LLVoiceVivox();

	void init(LLPumpIO* pump);
	void terminate();

	bool isVoiceWorking() const;

	// LLVoiceModule overrides
	LL_INLINE bool isVivox() const override				{ return true; }
	LL_INLINE bool isWebRTC() const override			{ return false; }
	const std::string& getName() const override;
	bool inProximalChannel() const override;
	void setSpatialChannel(const LLSD& channel_info) override;
	void setNonSpatialChannel(const LLSD& channel_info, bool, bool) override;
	void leaveNonSpatialChannel() override;
	void leaveAudioSession() override;
	void processChannels(bool enable) override;
	std::string sipURIFromID(const LLUUID& id) const override;

#if 0	// Not used in the Cool VL Viewer
	void setHidden(bool hidden) override;
#endif

	void setCaptureDevice(const std::string& device_id);
	void setRenderDevice(const std::string& device_id);

	LL_INLINE const strings_map_t& getCaptureDevices() const
	{
		return mCaptureDevices;
	}

	LL_INLINE const strings_map_t& getRenderDevices() const
	{
		return mRenderDevices;
	}

	void setTuningMode(bool tuning_on);
	bool inTuningMode();

	void tuningSetMicVolume(F32 volume);
#if 0	// Not used
	void tuningSetSpeakerVolume(F32 volume);
#endif
	LL_INLINE F32 tuningGetEnergy()						{ return mTuningEnergy; }

	bool deviceSettingsAvailable();
	void refreshDeviceLists(bool clear_current_list);

	// Used in mute list observer
	void muteListChanged();

	/////////////////////////////
	// Sending updates of current state

	bool isCurrentChannel(const LLSD& channel_info);
	bool compareChannels(const LLSD& channel_info1, const LLSD& channel_info2);

	void setVoiceVolume(F32 volume);
	void setMicGain(F32 volume);

	// Returns -1.f when 'id' does not correspond to a participant
	F32 getUserVolume(const LLUUID& id);
	// Sets volume for specified agent, from 0-1 (where .5 is nominal)
	void setUserVolume(const LLUUID& id, F32 volume);

	void setEarLocation(S32 loc);

	/////////////////////////////
	// Accessors for data related to nearby speakers

	bool getIsSpeaking(const LLUUID& id);

	// "power" is related to "amplitude" in a defined way. I am just not sure
	// what the formula is...
	// Note: changed to return -1.f when 'id' is not in session. HB
	F32 getCurrentPower(const LLUUID& id);

	bool getIsModeratorMuted(const LLUUID& id);

	// This is used by the string-keyed maps below, to avoid storing the string
	// twice. The 'const std::string*' in the key points to a string actually
	// stored in the object referenced by the map. The add & delete operations
	// for each map allocate and delete in the right order to avoid dangling
	// references. The default compare operation would just compare pointers,
	// which is incorrect, so they must use this comparator instead.
	struct stringMapComparator
	{
		LL_INLINE bool operator()(const std::string* a,
								  const std::string* b) const
		{
			return a->compare(*b) < 0;
		}
	};

	struct uuidMapComparator
	{
		LL_INLINE bool operator()(const LLUUID* a, const LLUUID* b) const
		{
			return *a < *b;
		}
	};

	class participantState
	{
	public:
		participantState(const std::string& uri);

		LL_INLINE bool isAvatar()						{ return mAvatarIDValid; }

		bool updateMuteState();

	public:
		LLUUID			mAvatarID;
		std::string		mURI;
		std::string		mAccountName;
		std::string		mLegacyName;
		std::string		mGroupID;
		LLFrameTimer	mSpeakingTimeout;
		S32				mVolume;
		S32				mUserVolume;
		F32				mPower;
		F32				mLastSpokeTimestamp;
		bool			mIsSelf;
		bool			mAvatarIDValid;
		bool			mPTT;
		bool			mIsSpeaking;
		bool			mIsModeratorMuted;

		// true if this avatar is on the user's mute list (and should be muted)
		bool			mOnMuteList;

		// true if this participant needs a volume command sent (either
		// mOnMuteList or mUserVolume has changed):
		bool			mVolumeDirty;
	};
	typedef std::map<const std::string*, participantState*,
					 stringMapComparator> particip_map_t;
	particip_map_t* getParticipantList();

	bool isParticipant(const LLUUID& id);

	void userAuthorized(const std::string& first_name,
						const std::string& last_name, const LLUUID& agent_id);

	void addObserver(LLVoiceClientStatusObserver* observerp);
	void removeObserver(LLVoiceClientStatusObserver* observerp);

	// Starts a voice session with the specified user
	void callUser(const LLUUID& id);

	// Leave the current channel
	void leaveChannel();

	bool answerInvite(std::string& session_handle);
	void declineInvite(std::string& session_handle);

	static bool getClientPath(std::string& exe_path, std::string& full_path);

private:
	enum streamState
	{
		streamStateUnknown = 0,
		streamStateIdle,
		streamStateConnected,
		streamStateRinging,
		// Same as Vivox session_media_connecting enum
		streamStateConnecting = 6,
		// Same as Vivox session_media_disconnecting enum
		streamStateDisconnecting = 7,
	};

	typedef std::map<const LLUUID*, participantState*,
					 uuidMapComparator> particip_id_map_t;
	typedef particip_id_map_t::value_type particip_id_map_val_t;

	class sessionState
	{
	public:
		sessionState();
		~sessionState();

		LLSD getVoiceChannelInfo() const;

		participantState* addParticipant(const std::string& uri);
		// Note: after removeParticipant returns, the participant* that was
		// passed to it will have been deleted.
		// Take care not to use the pointer again after that.
		void removeParticipant(participantState* participant);
		void removeAllParticipants();

		participantState* findParticipant(const std::string& uri);
		participantState* findParticipantByID(const LLUUID& id);

		bool isCallBackPossible();
		bool isTextIMPossible();

	public:
		S32					mErrorStatusCode;

		LLUUID				mIMSessionID;
		LLUUID				mCallerID;

		std::string			mHandle;
		std::string			mGroupHandle;
		std::string			mSIPURI;
		std::string			mAlias;
		std::string			mName;
		std::string			mAlternateSIPURI;
		std::string			mHash;			// Channel password
		std::string			mErrorStatusString;

		particip_map_t		mParticipantsByURI;
		particip_id_map_t	mParticipantsByUUID;

		// True if a Session.Create has been sent for this session and no
		// response has been received yet:
		bool				mCreateInProgress;
		// True if a Session.MediaConnect has been sent for this session and no
		// response has been received yet:
		bool				mMediaConnectInProgress;
		// True if a voice invite is pending for this session (usually waiting
		// on a name lookup):
		bool				mVoiceInvitePending;
		// True if the caller ID is a hash of the SIP URI (this means we should
		// not do a name lookup):
		bool				mSynthesizedCallerID;
		// True for both group and spatial channels (false for p2p, PSTN):
		bool				mIsChannel;
		bool				mIsSpatial;	// true for spatial channels
		bool				mIsP2P;
		bool				mIncoming;
		bool				mVoiceEnabled;
		bool				mProcessChannels;
		// Whether we should try to reconnect to this session if it's dropped:
		bool				mReconnect;
		// Set to true when the mute state of someone in the participant list
		// changes. The code will have to walk the list to find the changed
		// participant(s):
		bool				mVolumeDirty;
	};

	participantState* findParticipantByID(const LLUUID& id);

	sessionState* findSession(const std::string& handle);
	sessionState* findSessionBeingCreatedByURI(const std::string& uri);
	sessionState* findSession(const LLUUID& participant_id);

	sessionState* addSession(const std::string& uri,
							 const std::string& handle = LLStringUtil::null);

	void setSessionHandle(sessionState* sessionp,
						  const std::string& handle = LLStringUtil::null);

	void setSessionURI(sessionState* sessionp, const std::string& uri);
	void deleteSession(sessionState* sessionp);
	void deleteAllSessions();

	void verifySessionState();

	// This is called in several places where the session _may_ need to be
	// deleted. It contains logic for whether to delete the session or keep it
	// around.
	void reapSession(sessionState* sessionp);

	// Returns true if the session seems to indicate we've moved to a region on
	// a different voice server
	bool sessionNeedsRelog(sessionState* sessionp);

	// Pokes the state machine to shut down the connector and restart it.
	void requestRelog();

	void setVoiceEnabled(bool enabled);

	LL_INLINE void clearCaptureDevices()				{ mCaptureDevices.clear(); }
	void addCaptureDevice(const std::string& display_name,
						  const std::string& device_id);

	LL_INLINE void clearRenderDevices()					{ mRenderDevices.clear(); }
	void addRenderDevice(const std::string& display_name,
						 const std::string& device_id);

	void tuningRenderStartSendMessage(const std::string& name, bool loop);
	void tuningRenderStopSendMessage();

	void tuningCaptureStartSendMessage(S32 duration);
	void tuningCaptureStopSendMessage();

	// Call this if the connection to the daemon terminates unexpectedly. It
	// will attempt to reset everything and relaunch.
	void daemonDied();

	// Call this if we're just giving up on voice (can't provision an account,
	// etc). It will clean up and go away.
	void giveUp();

	/////////////////////////////
	// Session control messages
	void connectorCreate();
	void connectorShutdown();

	void closeSocket();

	void requestVoiceAccountProvision(S32 retries = 3);

	void login(const std::string& account_name, const std::string& password,
			   const std::string& sip_uri_hostname,
			   const std::string& account_server_uri);
	void loginSendMessage();
	void logout();
	void logoutSendMessage();

	// Pokes the state machine to leave the audio session next time around.
	void sessionTerminate();

	void lookupName(const LLUUID& id);
	static void onAvatarNameLookup(const LLUUID& id,
								   const std::string& fullname, bool);
	void avatarNameResolved(const LLUUID& id, const std::string& name);

	/////////////////////////////
	// Response/Event handlers
	void connectorCreateResponse(S32 status_code, std::string& status_str,
								 std::string& connector_handle,
								 std::string& version_id);
	void loginResponse(S32 status_code, std::string& status_str,
					   std::string& account_handle, S32 aliases_number);
	void sessionCreateResponse(std::string& request_id, S32 status_code,
							   std::string& status_str,
							   std::string& session_handle);
	void sessionGroupAddSessionResponse(std::string& request_id, S32 status_code,
										std::string& status_str,
										std::string& session_handle);
	void sessionConnectResponse(std::string& request_id, S32 status_code,
								std::string& status_str);
	void logoutResponse(S32 status_code, std::string& status_str);
	void connectorShutdownResponse(S32 status_code, std::string& status_str);

	void accountLoginStateChangeEvent(std::string& account_handle,
									  S32 status_code,
									  std::string& status_str, S32 state);
	void mediaStreamUpdatedEvent(std::string& session_handle,
								 std::string& session_grp_handle,
								 S32 status_code, std::string& status_str,
								 S32 state, bool incoming);
	void sessionAddedEvent(std::string& uri_str, std::string& alias,
						   std::string& session_handle,
						   std::string& session_grp_handle,
						   bool is_channel, bool incoming,
						   std::string& name_str);
	void sessionRemovedEvent(std::string& session_handle,
							 std::string& session_grp_handle);
	void participantAddedEvent(std::string& session_handle,
							   std::string& session_grp_handle,
							   std::string& uri_str, std::string& alias,
							   std::string& name_str,
							   std::string& display_name_str,
							   S32 participantType);
	void participantRemovedEvent(std::string& session_handle,
								 std::string& session_grp_handle,
								 std::string& uri_str, std::string& alias,
								 std::string& name_str);
	void participantUpdatedEvent(std::string& session_handle,
								 std::string& session_grp_handle,
								 std::string& uri_str, std::string& alias,
								 bool muted_by_moderator, bool speaking,
								 S32 volume, F32 energy);

	void messageEvent(std::string& session_handle, std::string& uri_str,
					  std::string& alias, std::string& msg_header,
					  std::string& msg_body);
	void sessionNotificationEvent(std::string& session_handle,
								  std::string& uri_str,
								  std::string& notif_type);

	LL_INLINE void auxAudioPropertiesEvent(F32 energy)	{ mTuningEnergy = energy; }

	void joinedAudioSession(sessionState* sessionp);
	void leftAudioSession(sessionState* sessionp);

	void sessionCreateSendMessage(sessionState* sessionp,
								  bool start_audio = true,
								  bool start_text = false);
	void sessionGroupAddSessionSendMessage(sessionState* sessionp,
										   bool start_audio = true,
										   bool start_text = false);

	// Just joins the audio session:
	void sessionMediaConnectSendMessage(sessionState* sessionp);

	// Just joins the text session:
	void sessionTextConnectSendMessage(sessionState* sessionp);

	void sessionGroupTerminateSendMessage(sessionState* sessionp);

	bool writeString(const std::string& str);

	void getCaptureDevicesSendMessage();
	void getRenderDevicesSendMessage();

	/////////////////////////////
	// Sending updates of current state

	void setCameraPosition(const LLVector3d& position,
						   const LLVector3& velocity,
						   const LLMatrix3& rot);
	void setAvatarPosition(const LLVector3d& position,
						   const LLVector3& velocity,
						   const LLMatrix3& rot);
	void updatePosition();

	// Internal state for a simple state machine. This is used to deal with the
	// asynchronous nature of some of the messages. Note: if you change this
	// list, please make corresponding changes to LLVoiceClient::state2string()
	enum state
	{
		stateDisableCleanup,
		stateDisabled,				// Voice is turned off.
		stateStart,					// Class is initialized, socket is created
		stateDaemonLaunched,		// Daemon has been launched
		stateConnecting,			// connect() call has been issued
		stateConnected,				// connection to the daemon has been made, send some initial setup commands.
		stateIdle,					// socket is connected, ready for messaging
		stateMicTuningStart,
		stateMicTuningRunning,
		stateMicTuningStop,
		stateConnectorStart,		// connector needs to be started
		stateConnectorStarting,		// waiting for connector handle
		stateConnectorStarted,		// connector handle received
		stateLoginRetry,			// need to retry login (failed due to changing password)
		stateLoginRetryWait,		// waiting for retry timer
		stateNeedsLogin,			// send login request
		stateLoggingIn,				// waiting for account handle
		stateLoggedIn,				// account handle received
		stateNoChannel,				//
		stateJoiningSession,		// waiting for session handle
		stateSessionJoined,			// session handle received
		stateRunning,				// in session, steady state
		stateLeavingSession,		// waiting for terminate session response
		stateSessionTerminated,		// waiting for terminate session response

		stateLoggingOut,			// waiting for logout response
		stateLoggedOut,				// logout response received
		stateConnectorStopping,		// waiting for connector stop
		stateConnectorStopped,		// connector stop received

		// We go to this state if the login fails because the account needs to
		// be provisioned.

		// Error states.  No way to recover from these yet.
		stateConnectorFailed,
		stateConnectorFailedWaiting,
		stateLoginFailed,
		stateLoginFailedWaiting,
		stateJoinSessionFailed,
		stateJoinSessionFailedWaiting,

		// Go here when all else has failed. Nothing will be retried, we are done.
		stateJail
	};

	void setState(state new_state);

	void stateMachine();

	// This should be called when the code detects we have changed parcels.
	// It initiates the call to the server that gets the parcel channel.
	void parcelChanged();

	void switchChannel(std::string uri = std::string(),
					   bool spatial = true, bool no_reconnect = false,
					   bool is_p2p = false, std::string hash = "");
	void joinSession(sessionState* sessionp);

	std::string sipURIFromName(std::string& name) const;

	bool inSpatialChannel() const;
	LLSD getAudioSessionChannelInfo() const;
	std::string getAudioSessionHandle() const;

	void sendPositionalUpdate();

	void buildSetCaptureDevice(std::ostringstream& stream);
	void buildSetRenderDevice(std::ostringstream& stream);
	void buildLocalAudioUpdates(std::ostringstream& stream);

	void setupVADParams();

	void enforceTether();

	void notifyParticipantObservers();
	void notifyStatusObservers(LLVoiceClientStatusObserver::EStatusType status);

	// This tries and kills the SLVoice daemon.
	void killDaemon();

	static void idle(void* user_data);

	static std::string nameFromID(const LLUUID& id);
	static bool IDFromName(const std::string name, LLUUID& id);

	// Returns the name portion of the SIP URI if the string looks vaguely
	// like a SIP URI, or an empty string if not.
	static std::string nameFromsipURI(const std::string& uri);

	static std::string state2string(state new_state);

	static void voiceAccountProvisionCoro(const std::string& url, S32 retries);
	static void parcelVoiceInfoRequestCoro(const std::string& url);

private:
	LLPumpIO*				mPump;
	LLProcessLauncher*		mProcess;

	state					mState;
	// Session state for the current audio session
	sessionState*			mAudioSession;
	// Session state for the audio session we are trying to join
	sessionState*			mNextAudioSession;

	U32						mRetries;

	U32						mLogLevel;

	S32						mLoginRetryCount;

	S32 					mNumberOfAliases;
	U32						mCommandCookie;

	S32						mSpeakerVolume;

	enum
	{
		earLocCamera = 0,		// Ear at camera
		earLocAvatar,			// Ear at avatar
		earLocMixed,				// Ear at avatar location/camera direction
		earLocSpeaker			// Singularity: listener matches speaker position
	};
	S32						mEarLocation;

	S32						mMicVolume;

	// State to return to when we leave tuning mode
	state					mTuningExitState;
	F32						mTuningEnergy;
	S32						mTuningMicVolume;
	S32						mTuningSpeakerVolume;

	LLHost					mDaemonHost;
	LLSocket::ptr_t			mSocket;

	std::string				mAccountName;
	std::string				mAccountPassword;
	std::string				mAccountDisplayName;
	std::string				mAccountFirstName;
	std::string				mAccountLastName;

	std::string				mTuningAudioFile;

	std::string				mSpatialSessionURI;
	std::string				mSpatialSessionCredentials;

	// Name of the channel to be looked up
	std::string				mChannelName;

	// Returned by "Create Connector" message
	std::string				mConnectorHandle;
	// Returned by login message
	std::string				mAccountHandle;

	std::string				mVoiceAccountServerURI;
	std::string				mVoiceSIPURIHostName;

	std::string				mCaptureDevice;
	std::string				mRenderDevice;

	// Active sessions, indexed by session handle. Sessions which are being
	// initiated may not be in this map.
	typedef std::map<const std::string*, sessionState*,
					 stringMapComparator> session_map_t;
	session_map_t			mSessionsByHandle;

	strings_map_t			mCaptureDevices;
	strings_map_t			mRenderDevices;

	std::string				mWriteString;

	LLVector3d				mCameraPosition;
	LLVector3d				mCameraRequestedPosition;
	LLVector3				mCameraVelocity;
	LLMatrix3				mCameraRot;

	LLVector3d				mAvatarPosition;
	LLVector3				mAvatarVelocity;
	LLMatrix3				mAvatarRot;

	LLTimer					mUpdateTimer;

	// All sessions, not indexed. This is the canonical session list.
	typedef std::set<sessionState*> session_set_t;
	typedef session_set_t::iterator session_set_it_t;
	session_set_t			mSessions;

	typedef std::set<LLVoiceClientStatusObserver*> status_observer_set_t;
	status_observer_set_t	mStatusObservers;

	bool					mTerminated;

	bool					mVoiceEnabled;
	bool					mProcessChannels;
	bool					mAccountLoggedIn;
	bool					mConnectorEstablished;
#if LL_LINUX
	// When true, denotes the use of the deprecated native Linux client
	bool					mDeprecatedClient;
#endif

	bool					mConnected;
	bool					mSessionTerminateRequested;
	bool					mRelogRequested;

	bool					mCaptureDeviceDirty;
	bool					mRenderDeviceDirty;

	bool					mTuningMode;
	bool					mTuningMicVolumeDirty;
	bool					mTuningSpeakerVolumeDirty;

	bool					mSpatialCoordsDirty;
	bool					mSpeakerVolumeDirty;
	bool					mSpeakerMuteDirty;
	bool					mMicVolumeDirty;
	bool					mPTT;
	bool					mPTTDirty;
};

// Note: since this is a global class instance, all class members are always
// available, from viewer launch to _exit(): no need for static variables to
// track the shutdown (or ready) state !  HB
extern LLVoiceVivox gVoiceVivox;
