/**
 * @file llcoproceduremanager.h
 * @author Rider Linden
 * @brief Singleton class for managing asset uploads to the sim.
 *
 * $LicenseInfo:firstyear=2015&license=viewergpl$
 *
 * Copyright (c) 2015-2022, Linden Research, Inc.
 * Copyright (c) 2019-2025, Henri Beauchamp.
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

#include "llcorehttputil.h"
#include "llsingleton.h"

class LLCoprocedurePool;

class LLCoprocedureManager final : public LLSingleton<LLCoprocedureManager>
{
	friend class LLSingleton<LLCoprocedureManager>;

protected:
	LOG_CLASS(LLCoprocedureManager);

public:
	typedef std::function<U32(const std::string&)> setting_query_t;

	typedef std::function<void(const std::string&, U32)> setting_upd_t;

	typedef std::function<void(LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t&,
							   const LLUUID&)> coprocedure_t;

	LLCoprocedureManager() = default;
	~LLCoprocedureManager();

	// Places a coprocedure on the queue for processing. 'name' is used for
	// debugging and should identify this coroutine. 'proc' is a bound function
	// to be executed. This method returns a generated UUID on success and a
	// null UUID on failure to enqueue 'proc' (queue mutex locked by some other
	// fiber or thread): the caller should then attempt a new enqueuing at next
	// frame.
	LLUUID enqueueCoprocedure(const std::string& pool, const std::string& name,
							  coprocedure_t proc);

	void setPropertyMethods(setting_query_t queryfn, setting_upd_t updatefn);

	// Requests an exit for all the coprocedure manager coroutines.
	void cleanup();

	// Returns the number of coprocedures in the queue awaiting processing.
	U32 countPending() const;
	U32 countPending(const std::string& pool) const;

	// Returns the number of coprocedures actively being processed.
	U32 countActive() const;
	U32 countActive(const std::string& pool) const;

	// Returns the total number of coprocedures either queued or in active
	// processing.
	U32 count() const;
	U32 count(const std::string& pool) const;

	typedef std::shared_ptr<LLCoprocedurePool> pool_ptr_t;
	pool_ptr_t initializePool(const std::string& pool_name);

private:
	typedef std::map<std::string, pool_ptr_t, std::less<> > pool_map_t;
	pool_map_t		mPoolMap;

	setting_query_t	mPropertyQueryFn;
	setting_upd_t	mPropertyDefineFn;
};
