/**
 * @file llmaterialmgr.h
 * @brief Material manager
 *
 * $LicenseInfo:firstyear=2012&license=viewergpl$
 *
 * Copyright (c) 2012, Linden Research, Inc.
 * Copyright (c) 2012-2024, Henri Beauchamp.
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

#include <memory>

#include "boost/signals2.hpp"

#include "llcorehttputil.h"
#include "hbfastmap.h"
#include "llmaterial.h"
#include "llmaterialid.h"
#include "llsingleton.h"
#include "lluuid.h"

class LLViewerRegion;

class LLMaterialMgr : public LLSingleton<LLMaterialMgr>
{
	friend class LLSingleton<LLMaterialMgr>;

protected:
	LOG_CLASS(LLMaterialMgr);

	LLMaterialMgr();
	virtual ~LLMaterialMgr();

public:
	typedef fast_hmap<LLMaterialID, LLMaterialPtr> material_map_t;
	typedef boost::signals2::signal<void(const LLMaterialID&,
										 const LLMaterialPtr)> get_cb_t;
	typedef boost::signals2::signal<void(const LLMaterialID&,
										 const LLMaterialPtr,
										 U32 te)> get_te_cb_t;
	typedef boost::signals2::signal<void(const LLUUID&,
										 const material_map_t&)> get_all_cb_t;

	const LLMaterialPtr get(const LLUUID& region_id,
							const LLMaterialID& material_id);
	void get(const LLUUID& region_id, const LLMaterialID& material_id,
			 get_cb_t::slot_type cb);
	void getTE(const LLUUID& region_id, const LLMaterialID& material_id,
			   U32 te, get_te_cb_t::slot_type cb);

	void getAll(const LLUUID& region_id);
	void getAll(const LLUUID& region_id, get_all_cb_t::slot_type cb);
	void put(const LLUUID& object_id, U8 te, const LLMaterial& material);
	void remove(const LLUUID& object_id, U8 te);

	// Explicitly add new material to material manager
	void setLocalMaterial(const LLUUID& region_id, LLMaterialPtr material_ptr);

	// To be called on viewer exit, to cleanup boost signal callbacks and
	// material queues. HB
	static void cleanupClass();

private:
	void clearGetQueues(const LLUUID& region_id);
	bool isGetPending(const LLUUID& region_id,
					  const LLMaterialID& material_id) const;
	bool isGetAllPending(const LLUUID& region_id) const;
	void markGetPending(const LLUUID& region_id,
						const LLMaterialID& material_id);
	const LLMaterialPtr setMaterial(const LLUUID& region_id,
									const LLMaterialID& material_id,
									const LLSD& material_data);
	void setMaterialCallbacks(const LLMaterialID& material_id,
							  const LLMaterialPtr& material_ptr);

	static void onIdle(void*);

	void processGetQueue();
	void onGetResponse(bool success, const LLSD& content,
					   const LLUUID& region_id);
	void processGetAllQueue();
	void processGetAllQueueCoro(const std::string& url, LLUUID region_id);
	void onGetAllResponse(bool success, const LLSD& content,
						  const LLUUID& region_id);
	void processPutQueue();
	void onPutResponse(bool success, const LLSD& content);
	void onRegionRemoved(LLViewerRegion* regionp);

public:
	// Class for TE-specific material ID query
	class TEMaterialPair
	{
	public:
		LL_INLINE bool operator==(const TEMaterialPair& rhs) const
		{
			return mMaterialId == rhs.mMaterialId && mTE == rhs.mTE;
		}

	public:
		U32				mTE;
		LLMaterialID	mMaterialId;
	};

	friend LL_INLINE bool operator<(const LLMaterialMgr::TEMaterialPair& lhs,
									const LLMaterialMgr::TEMaterialPair& rhs)
	{
		return lhs.mTE < rhs.mTE ? true : lhs.mMaterialId < rhs.mMaterialId;
	}

	// Class for pending material in a given region
	class RegionMaterialPair
	{
	public:
		LL_INLINE RegionMaterialPair(const LLUUID& region_id,
									 const LLMaterialID& material_id)
		:	mRegionId(region_id),
			mMaterialId(material_id)
		{
		}

		LL_INLINE bool operator==(const RegionMaterialPair& rhs) const
		{
			return mRegionId == rhs.mRegionId &&
				   mMaterialId == rhs.mMaterialId;
		}

	public:
		LLUUID			mRegionId;
		LLMaterialID	mMaterialId;
	};

	friend LL_INLINE bool operator<(const LLMaterialMgr::RegionMaterialPair& lhs,
									const LLMaterialMgr::RegionMaterialPair& rhs)
	{
		return lhs.mRegionId < rhs.mRegionId ? true
											 : lhs.mMaterialId < rhs.mMaterialId;
	}

private:
	material_map_t								mMaterials;

	uuid_list_t									mGetAllQueue;
	uuid_list_t									mGetAllRequested;

	typedef std::set<LLMaterialID> material_queue_t;
	typedef fast_hmap<LLUUID, material_queue_t> get_queue_t;
	get_queue_t									mGetQueue;

	typedef fast_hmap<RegionMaterialPair, F64> get_pending_map_t;
	get_pending_map_t							mGetPending;

	typedef fast_hmap<LLUUID, F64> getall_pending_map_t;
	getall_pending_map_t						mGetAllPending;

	typedef fast_hmap<LLMaterialID, std::unique_ptr<get_cb_t> > get_cb_map_t;
	get_cb_map_t								mGetCallbacks;

	typedef fast_hmap<TEMaterialPair,
					  std::unique_ptr<get_te_cb_t> > get_te_cb_map_t;
	get_te_cb_map_t								mGetTECallbacks;

	typedef fast_hmap<LLUUID,
					  std::unique_ptr<get_all_cb_t> > get_all_cb_map_t;
	get_all_cb_map_t							mGetAllCallbacks;

	typedef fast_hmap<U8, LLMaterial> facematerial_map_t;
	typedef fast_hmap<LLUUID, facematerial_map_t> put_queue_t;
	put_queue_t									mPutQueue;

	LLCore::HttpRequest::ptr_t					mHttpRequest;
	LLCore::HttpHeaders::ptr_t					mHttpHeaders;
	LLCore::HttpOptions::ptr_t					mHttpOptions;
	LLCore::HttpRequest::policy_t				mHttpPolicy;
	LLCoreHttpUtil::HttpCoroutineAdapter::ptr_t	mHttpAdapter;
};

// std::hash implementation for TEMaterialPair. HB
namespace std
{
	template<> struct hash<LLMaterialMgr::TEMaterialPair>
	{
		LL_INLINE size_t operator()(const LLMaterialMgr::TEMaterialPair& p) const noexcept
		{
			return (p.mTE + 1) * p.mMaterialId.getDigest64();
		}
	};
}

// For use with boost::unordered_map and boost::unordered_set. HB
LL_INLINE size_t hash_value(const LLMaterialMgr::TEMaterialPair& p) noexcept
{
	return (p.mTE + 1) * p.mMaterialId.getDigest64();
}

// std::hash implementation for RegionMaterialPair. HB
namespace std
{
	template<> struct hash<LLMaterialMgr::RegionMaterialPair>
	{
		LL_INLINE size_t operator()(const LLMaterialMgr::RegionMaterialPair& p) const noexcept
		{
			return p.mRegionId.getDigest64() ^ p.mMaterialId.getDigest64();
		}
	};
}

// For use with boost::unordered_map and boost::unordered_set. HB
LL_INLINE size_t hash_value(const LLMaterialMgr::RegionMaterialPair& p) noexcept
{
	return p.mRegionId.getDigest64() ^ p.mMaterialId.getDigest64();
}
