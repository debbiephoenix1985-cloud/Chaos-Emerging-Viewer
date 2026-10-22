/**
 * @file llthread.h
 * @brief Base classes for thread, mutex and condition handling.
 *
 * $LicenseInfo:firstyear=2004&license=viewergpl$
 *
 * Copyright (c) 2004-2009, Linden Research, Inc.
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

#include <thread>

#include "llapp.h"
#include "llmutex.h"
#include "llrefcount.h"

bool is_main_thread();
void assert_main_thread();

class LLThread
{
	friend class LLMutex;

protected:
	LOG_CLASS(LLThread);

public:
	typedef std::thread::id id_t;

	typedef enum e_thread_status
	{
		STOPPED  = 0, // Thread is not running: not started or exited run()
		RUNNING  = 1, // Thread is currently running
		QUITTING = 2  // Someone wants this thread to quit
	} EThreadStatus;

	LLThread(const std::string& name);

	// Warning !  You almost NEVER want to destroy a thread unless it is in the
	// STOPPED state.
	virtual ~LLThread();

	virtual void shutdown(); // Stops the thread

	LL_INLINE bool isRunning() const					{ return mStatus == RUNNING; }
	LL_INLINE bool isQuitting() const					{ return mStatus == QUITTING; }
	LL_INLINE bool isStopped() const					{ return mStatus == STOPPED; }

	// PAUSE / RESUME functionality. See source code for important usage notes.
	// Called from MAIN THREAD.
	void pause();
	void unpause();
	LL_INLINE bool isPaused() const						{ return isStopped() || mPaused; }

	// Cause the thread to wake up and check its condition
	void wake();

	// Same as above, but to be used when the condition is already locked.
	void wakeLocked();

	// Called from run() (CHILD THREAD). Pause the thread if requested until
	// unpaused.
	void checkPause();

	// This kicks off the thread
	void start();

	// Note: returns a "not a thread" value until threadRun() is called.
	LL_INLINE id_t getID() const						{ return mID; }

	// Sets the maximum number of retries after a thread run() threw an
	// exception
	LL_INLINE void setRetries(U32 n)					{ mRetries = n + 1; }

	// Static because it can be called by the main thread, which does not have
	// an LLThread data structure.
	static void yield();

	// Returns the ID of the current thread
	static id_t currentID();

	static U64 thisThreadIdHash();

	static void setThreadName(std::string thread_name);

protected:
	void setQuitting();

	// Virtual function overridden by subclass; this is called when the thread
	// runs
	virtual void run() = 0;

	// Virtual predicate function: returns true if the thread should wake up,
	// false if it should sleep.
	virtual bool runCondition();

	// Lock/unlock Run Condition: use around modification of any variable used
	// in runCondition()
	LL_INLINE void lockData()							{ mDataLock->lock(); }
	LL_INLINE void unlockData()							{ mDataLock->unlock(); }
	// This is the predicate that decides whether the thread should sleep.
	// It should only be called with mDataLock locked, since the virtual
	// runCondition() function may need to access data structures that are
	// thread-unsafe.
	// To avoid spurious signals (and the associated context switches) when the
	// condition may or may not have changed, you can do the following:
	// mDataLock->lock();
	// if (!shouldSleep())
	//     mRunCondition->signal();
	// mDataLock->unlock();
	LL_INLINE bool shouldSleep()
	{
		return mStatus == RUNNING && (isPaused() || !runCondition());
	}

private:
	// Paranoid (actually needed) check for spuriously deleted threads.
	static bool isThreadLive(LLThread* threadp);

	void threadRun();

protected:
	LLMutex*			mDataLock;
	class LLCondition*	mRunCondition;
	std::thread*		mThreadp;
	std::string			mName;
	id_t				mID;
	EThreadStatus		mStatus;

private:
	// For termination in case of issues
	typedef std::thread::native_handle_type handle_t;
	handle_t			mNativeHandle;
	U32					mRetries;
	bool				mPaused;
	bool				mNeedsAffinity;
};
