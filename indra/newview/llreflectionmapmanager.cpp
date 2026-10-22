/**
 * @file llreflectionmapmanager.cpp
 * @brief LLReflectionMap, LLReflectionMapManager and LLHeroProbeManager
 * classes implementation.
 *
 * $LicenseInfo:firstyear=2022&license=viewergpl$
 *
 * Copyright (c) 2022, Linden Research, Inc.
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

#include "llreflectionmapmanager.h"

#include "hbtracy.h"

#include "llagent.h"
#include "llappviewer.h"
#if LL_RESET_HDRI_SKY_ON_REFLECTION_MAP_RESET
# include "lldrawpoolwlsky.h"		// For LLDrawPoolWLSky::resetHDRISky()
#endif
#include "llenvironment.h"
#include "llpipeline.h"
#include "llspatialpartition.h"
#include "llstartup.h"
#include "llviewercamera.h"
#include "llviewercontrol.h"
#include "llviewerdisplay.h"		// For gTeleportDisplay, gCubeSnapshot
#include "llviewerregion.h"
#include "llviewershadermgr.h"
#include "llviewerwindow.h"
#include "llvoavatarself.h"
#include "llvovolume.h"
#include "llworld.h"

// Uniform names
static LLStaticHashedString sDirection("direction");
static LLStaticHashedString sResScale("resScale");
static LLStaticHashedString sRoughness("roughness");
static LLStaticHashedString sSourceIdx("sourceIdx");
static LLStaticHashedString sWidth("u_width");
static LLStaticHashedString sZnear("znear");
static LLStaticHashedString sZfar("zfar");

///////////////////////////////////////////////////////////////////////////////
// LLReflectionMap class
///////////////////////////////////////////////////////////////////////////////

LLReflectionMap::LLReflectionMap()
:	mCubeIndex(-1),
	mDistance(-1.f),
	mMinDepth(-1.f),
	mMaxDepth(-1.f),
	mRadius(16.f),
	mLastUpdateTime(0.f),
	mLastBindTime(0.f),
	mFadeIn(0.f),
	mProbeIndex(-1),
	mPriority(0),
	mOcclusionQuery(0),
	mOccluded(false),
	mComplete(false)
{
	mOrigin.clear();
}

LLReflectionMap::~LLReflectionMap()
{
	if (mOcclusionQuery)
	{
		gPipeline.mReflectionMapManager.recycleQuery(mOcclusionQuery);
	}
	mGroup = NULL;
	mViewerObject = NULL;
}

void LLReflectionMap::update(U32 resolution, U32 face, bool force_dynamic,
							 F32 near_clip, bool use_clip_plane,
							 LLPlane clip_plane, const LLVector3* lookatp,
							 const LLVector3* updirp)
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	if (!gUsePBRShaders || mCubeIndex == -1 || mCubeArray.isNull())
	{
		return;
	}
	mLastUpdateTime = gFrameTimeSeconds;
	// Make sure we do not walk off the edge of the render target
	while (resolution > gPipeline.mRT->mDeferredScreen.getWidth() ||
		   resolution > gPipeline.mRT->mDeferredScreen.getHeight())
	{
		resolution /= 2;
	}
	if (near_clip <= 0.f)
	{
		near_clip = getNearClip();
	}
	gViewerWindowp->cubeSnapshot(LLVector3(mOrigin.getF32ptr()), mCubeArray,
								 face, near_clip,
								 force_dynamic || getIsDynamic(),
								 use_clip_plane, clip_plane, lookatp, updirp);
}

void LLReflectionMap::autoAdjustOrigin()
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	if (mComplete || mGroup.isNull() ||
		mGroup->hasState(LLViewerOctreeGroup::DEAD))
	{
		if (mViewerObject.notNull() && !mViewerObject->isDead())
		{
			mPriority = 1;
			mOrigin.load3(mViewerObject->getPositionAgent().mV);
			if (mViewerObject->getVolume())
			{
				LLVOVolume* vobjp = mViewerObject->asVolume();
				if (vobjp && vobjp->getReflectionProbeIsBox())
				{
					static const LLVector3 half(0.5f, 0.5f, 0.5f);
					mRadius = vobjp->getScale().scaledVec(half).length();
					return;
				}
			}
			mRadius = mViewerObject->getScale().mV[0] * 0.5f;
		}
		return;
	}

	LLSpatialPartition* partp = mGroup->getSpatialPartition();
	if (!partp || partp->mPartitionType != LLViewerRegion::PARTITION_VOLUME)
	{
		return;
	}

	mPriority = 0;

	if (!mGroup->getOctreeNode())
	{
		return;
	}

	// Cast a ray towards 8 corners of bounding box nudge origin towards center
	// of empty space
	const LLVector4a* bounds = mGroup->getBounds();
	mOrigin = bounds[0];
	LLVector4a size = bounds[1];

	LLVector4a corners[] =
	{
		{ 1.f, 1.f, 1.f },
		{ -1.f, 1.f, 1.f },
		{ 1.f, -1.f, 1.f },
		{ -1.f, -1.f, 1.f },
		{ 1.f, 1.f, -1.f },
		{ -1.f, 1.f, -1.f },
		{ 1.f, -1.f, -1.f },
		{ -1.f, -1.f, -1.f }
	};
	for (U32 i = 0; i < 8; ++i)
	{
		corners[i].mul(size);
		corners[i].add(bounds[0]);
	}

	LLVector4a extents[2];
	extents[0].setAdd(bounds[0], bounds[1]);
	extents[1].setSub(bounds[0], bounds[1]);

	// Prevent click-through exceptions to kick in during the calls to
	// lineSegmentIntersect(). HB
	gPickingProbe = true;
	LLVector4a intersection;
	bool hit = false;
	for (U32 i = 0; i < 8; ++i)
	{
		S32 face = -1;
		LLDrawable* drawablep =
			mGroup->lineSegmentIntersect(bounds[0], corners[i], false, false,
										 &face, &intersection);
		if (drawablep)
		{
			hit = true;
			update_min_max(extents[0], extents[1], intersection);
		}
		else
		{
			update_min_max(extents[0], extents[1], corners[i]);
		}
	}
	gPickingProbe = false;

	if (hit)
	{
		mOrigin.setAdd(extents[0], extents[1]);
		mOrigin.mul(0.5f);
	}

	// Make sure origin is not under the ground
	F32* fp = mOrigin.getF32ptr();
	LLVector3 origin(fp);
	F32 height = gWorld.resolveLandHeightAgent(origin) + 2.f;
	fp[2] = llmax(fp[2], height);

	// Make sure radius encompasses all objects
	LLSimdScalar r2 = 0.0;
	for (S32 i = 0; i < 8; ++i)
	{
		LLVector4a v;
		v.setSub(corners[i], mOrigin);

		LLSimdScalar d = v.dot3(v);
		if (d > r2)
		{
			r2 = d;
		}
	}

	mRadius = llmax(sqrtf(r2.getF32()), 8.f);

	// Make sure near clip does not poke through ground
	fp[2] = llmax(fp[2], height + mRadius * 0.5f);
}

bool LLReflectionMap::intersects(const LLReflectionMap* otherp) const
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	LLVector4a delta;
	delta.setSub(otherp->mOrigin, mOrigin);

	F32 r = mRadius + otherp->mRadius;
	return delta.dot3(delta).getF32() < r * r;
}

F32 LLReflectionMap::getAmbiance()
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	F32 ret = 0.f;
	if (mViewerObject.notNull() && !mViewerObject->isDead() &&
		mViewerObject->getVolume())
	{
		LLVOVolume* vobjp = mViewerObject->asVolume();
		if (vobjp)
		{
			ret = vobjp->getReflectionProbeAmbiance();
		}
	}
	return ret;
}

F32 LLReflectionMap::getNearClip()
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	F32 ret = 1.f;	// Default to 1m for automatic terrain probes
	if (mViewerObject.notNull() && !mViewerObject->isDead() &&
		mViewerObject->getVolume())
	{
		LLVOVolume* vobjp = mViewerObject->asVolume();
		if (vobjp)
		{
			ret = vobjp->getReflectionProbeNearClip();
		}
	}
	else if (mGroup.notNull())
	{
		// Default to half radius for automatic object probes
		ret = mRadius * 0.5f;
	}
	constexpr F32 MINIMUM_NEAR_CLIP = 0.1f;
	return llmax(ret, MINIMUM_NEAR_CLIP);
}

bool LLReflectionMap::getIsDynamic()
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	static LLCachedControl<U32> probe_detail(gSavedSettings,
											 "RenderReflectionProbes");
	if (mViewerObject.isNull() || mViewerObject->isDead() ||
		!mViewerObject->getVolume() || (U32)probe_detail < STATIC_AND_DYNAMIC)
	{
		return false;
	}
	LLVOVolume* vovolp = mViewerObject->asVolume();
	return vovolp && vovolp->getReflectionProbeIsDynamic();
}

bool LLReflectionMap::getBox(LLMatrix4& box)
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	if (mViewerObject.isNull() || mViewerObject->isDead() ||
		!mViewerObject->getVolume())
	{
		return false;
	}

	LLVOVolume* vobjp = mViewerObject->asVolume();
	if (!vobjp || !vobjp->mDrawable || vobjp->mDrawable->isDead() ||
		!vobjp->getReflectionProbeIsBox())
	{
		return false;
	}

	static const LLVector3 half(0.5f, 0.5f, 0.5f);
	LLVector3 s = vobjp->getScale().scaledVec(half);
	mRadius = s.length();

	// Object to agent space (no scale)
	LLMatrix4a scale;
	scale.setIdentity();
	scale.applyScaleAffine(s);
	scale.transpose();

	// Construct object to camera space (with scale)
	LLMatrix4a mv = gGLModelView;
	LLMatrix4a rm(vobjp->mDrawable->getWorldMatrix());
	mv.mul(rm);
	mv.mul(scale);

	// Inverse is camera space to object unit cube
	mv.invert();
	box.set(mv.getF32ptr());

	return true;
}

bool LLReflectionMap::isRelevant()
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	static LLCachedControl<U32> reflections(gSavedSettings,
											"RenderReflectionProbes");
	if (!reflections)
	{
		return false;
	}

	static LLCachedControl<U32> probe_level(gSavedSettings,
											"RenderReflectionProbeLevel");
	if (probe_level >= 3)
	{
		// All probes are relevant
		return true;
	}

	if (probe_level > 0 && mViewerObject.notNull() && !mViewerObject->isDead())
	{
		// Not an automatic probe
		return true;
	}

	if (probe_level == 2)
	{
		// Terrain and water only, ignore probes that have a group
		return mGroup.isNull();
	}

	return false;
}

// Super sloppy, but we are doing an occlusion cull against a bounding cube of
// a bounding sphere: pad radius so we assume if the eye is within the bounding
// sphere of the bounding cube, the node is not culled.
void LLReflectionMap::doOcclusion(const LLVector4a& eye)
{
	LL_TRACY_TIMER(TRC_REFLECTION_MAP);

	if (LLGLSLShader::sProfileEnabled)
	{
		return;
	}

	F32 dist = mRadius * F_SQRT3 + 1.f;

	LLVector4a o;
	o.setSub(mOrigin, eye);

	bool do_query = false;

	if (o.getLength3().getF32() < dist)
	{
		// Eye is inside radius, do not attempt to occlude
		mOccluded = false;
		return;
	}

	if (mOcclusionQuery == 0)
	{
		// No query was previously issued, allocate one and issue
		mOcclusionQuery = gPipeline.mReflectionMapManager.allocateQuery();
		do_query = true;
		// Assume not occluded until we get the query result. HB
		mOccluded = false;
	}
	else
	{
		// Query was previously issued, check it and only issue a new query
		// if previous query is available
		GLuint result = 0;
		glGetQueryObjectuiv(mOcclusionQuery, GL_QUERY_RESULT_AVAILABLE, &result);

		if (result > 0)
		{
			do_query = true;
			glGetQueryObjectuiv(mOcclusionQuery, GL_QUERY_RESULT, &result);
			mOccluded = result == 0;
		}
	}

	if (do_query)
	{
		glBeginQuery(GL_ANY_SAMPLES_PASSED, mOcclusionQuery);

		LLGLSLShader* shaderp = LLGLSLShader::sCurBoundShaderPtr;
		if (!shaderp)
		{
			gOcclusionCubeProgram.bind();
			shaderp = LLGLSLShader::sCurBoundShaderPtr;
		}
		shaderp->uniform3fv(LLShaderMgr::BOX_CENTER, 1, mOrigin.getF32ptr());
		shaderp->uniform3f(LLShaderMgr::BOX_SIZE, mRadius, mRadius, mRadius);

		gPipeline.mCubeVB->drawRange(LLRender::TRIANGLE_FAN, 0, 7, 8,
									 get_box_fan_indices(&gViewerCamera,
														 mOrigin));

		glEndQuery(GL_ANY_SAMPLES_PASSED);
	}
}

///////////////////////////////////////////////////////////////////////////////
// LLReflectionMapManager class
///////////////////////////////////////////////////////////////////////////////

//static
F32 LLReflectionMapManager::sDefaultDistance = 64.f;
bool LLReflectionMapManager::sNeedsReregister = false;

static U32 sUpdateCount = 0;

// Helper function
static void touch_default_probe(LLReflectionMap* probep)
{
	LLVector3 origin = gViewerCamera.getOrigin();
	origin.mV[2] += LLReflectionMapManager::sDefaultDistance;
	probep->mOrigin.load3(origin.mV);
}

// Structure for packing uniform buffer object.
// See class3/deferred/reflectionProbeF.glsl
struct LLReflectionProbeData
{
	// For box probes, matrix that transforms from camera space to a [-1, 1]
	// cube representing the bounding box of the box probe
	LLMatrix4 refBox[LL_MAX_REFLECTION_PROBE_COUNT];

	LLMatrix4 heroBox;

	// For sphere probes, origin (xyz) and radius (w) of refmaps in clip space
	LLVector4 refSphere[LL_MAX_REFLECTION_PROBE_COUNT];

	// Extra parameters
	//  x - irradiance scale
	//  y - radiance scale
	//  z - fade in
	//  w - znear
	LLVector4 refParams[LL_MAX_REFLECTION_PROBE_COUNT];

	LLVector4 heroSphere;

	// Indices used by probe:
	//  [i][0] - cubemap array index for this probe
	//  [i][1] - index into "refNeighbor" for probes that intersect this probe
	//  [i][2] - number of probes  that intersect this probe, or -1 for no
	//			 neighbors
	//  [i][3] - priority (probe type stored in sign bit - positive for
	//			 spheres, negative for boxes)
	GLint refIndex[LL_MAX_REFLECTION_PROBE_COUNT][4];

	// List of neighbor indices
	GLint refNeighbor[4096];

	// Lookup table for which index to start with for the given Z depth
	GLint refBucket[256][4];
	// Numbrer of active refmaps
	GLint refmapCount;

	GLint heroShape;
	GLint heroMipCount;
	GLint heroProbeCount;

	LLMatrix4 heroPlaneMatrix;
};

LLReflectionMapManager::LLReflectionMapManager()
:	mProbeData(new LLReflectionProbeData),
	mUpdatingProbe(NULL),
	mUBO(0),
	mUpdatingFace(0),
	mReflectionProbeCount(0),
	mDynamicProbeCount(LL_MAX_REFLECTION_PROBE_COUNT),
	mProbeResolution(128),
	mMaxProbeLOD(6.f),
	mLightScale(1.f),
	mResetFade(1.f),
	mResumeTime(0.f),
	mReset(false),
	mPaused(false),
	mRadiancePass(false),
	mRealtimeRadiancePass(false)
{
	initCubeFree();
}

LLReflectionMapManager::~LLReflectionMapManager()
{
	delete mProbeData;
	mProbeData = NULL;
}

void LLReflectionMapManager::initCubeFree()
{
	// Start at 1 because index 0 is reserved for mDefaultProbe
	for (U32 i = 1; i < mDynamicProbeCount; ++i)
	{
		mCubeFree.push_back(i);
	}
}

struct CompareProbeDistance
{
	LL_INLINE bool operator()(const LLPointer<LLReflectionMap>& lhs,
							  const LLPointer<LLReflectionMap>& rhs)
	{
		return lhs->mDistance < rhs->mDistance;
	}
};

static F32 update_score(LLReflectionMap* probep)
{
	return gFrameTimeSeconds - probep->mLastUpdateTime -
		   probep->mDistance * 0.1f;
}

// Returns true if a is higher priority for an update than b
static bool check_priority(LLReflectionMap* a, LLReflectionMap* b)
{
	if (a->mCubeIndex == -1)
	{
		// Not a candidate for updating
		return false;
	}
	if (b->mCubeIndex == -1)
	{
		// b is not a candidate for updating, a is higher priority by default
		return true;
	}
	if (!a->mComplete && !b->mComplete)
	{
		// Neither probe is complete, use distance
		return a->mDistance < b->mDistance;
	}
	if (a->mComplete && b->mComplete)
	{
		// Both probes are complete, use update_score metric
		return update_score(a) > update_score(b);
	}
	if (sUpdateCount % 3 == 0)
	{
		// a or b  is not complete; every third update, allow complete probes
		// to cut in line in front of non-complete probes to avoid spammy probe
		// generators from deadlocking scheduler (SL-20258).
		return !b->mComplete;
	}
	// Prioritize incomplete probe
	return b->mComplete;
}

void LLReflectionMapManager::update()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!LLPipeline::sReflectionProbesEnabled || gCubeSnapshot ||
		gTeleportDisplay || gDisconnected || !LLStartUp::isLoggedIn() ||
		gAppViewerp->logoutRequestSent())
	{
		llassert(!gCubeSnapshot); // Assert a snapshot is not in progress
		return;
	}

	if (mPaused && gFrameTimeSeconds > mResumeTime)
	{
		mPaused = false;
	}

	static LLCachedControl<U32> detail(gSavedSettings,
									   "RenderReflectionProbes");
	static LLCachedControl<U32> pcount(gSavedSettings,
									   "RenderReflectionProbeCount");
	static LLCachedControl<S32> dalloc(gSavedSettings,
									   "RenderReflectionProbeDynamicAlloc");
	U32 last_probe_count = mDynamicProbeCount;
	U32 new_probe_count;
	if (dalloc > -1)
	{
		switch ((U32)detail)
		{
			case 0:		new_probe_count = 1; break;
			case 1:		new_probe_count = mProbes.size(); break;
			case 2:		new_probe_count = llmax(128, mProbes.size()); break;
			default:	new_probe_count = 256;
		}
		if (dalloc > 1)
		{
			// Round mDynamicProbeCount to the nearest increment of 16
			new_probe_count = ((new_probe_count + dalloc / 2) / dalloc) * 16;
		}
		else
		{
			new_probe_count += dalloc;
		}
	}
	else
	{
		new_probe_count = pcount;
	}
	// Clamp the result (not using llclamp() here, because pcount could be 0).
	mDynamicProbeCount = llmax(1, llmin(new_probe_count, pcount));
	if (mDynamicProbeCount != last_probe_count)
	{
		mResetFade = 1.f;
	}

	initReflectionMaps();

	llassert(mProbes[0] == mDefaultProbe);

	LLVector4a camera_pos;
	camera_pos.load3(gViewerCamera.getOrigin().mV);

	// Process kill list
	for (U32 i = 0, count = mKillList.size(); i < count; ++i)
	{
		const auto& probep = mKillList[i];
		prmap_vec_t::const_iterator start = mProbes.begin();
		prmap_vec_t::const_iterator end = mProbes.end();
		prmap_vec_t::const_iterator iter = std::find(start, end, probep);
		if (iter != end)
		{
			deleteProbe(iter - start);
		}
	}
	mKillList.clear();

	// Process create list
	for (U32 i = 0, count = mCreateList.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mCreateList[i].get();
		if (probep)	// Paranoia
		{
			mProbes.emplace_back(probep);
		}
	}
	mCreateList.clear();

	if (mProbes.empty())
	{
		return;
	}

	bool did_update = false;

	static LLCachedControl<U32> probe_level(gSavedSettings,
											"RenderReflectionProbeLevel");
	U32 level = detail ? probe_level : 0;

	bool realtime = (U32)detail >= LLReflectionMap::REALTIME;

	if (mUpdatingProbe)
	{
		did_update = true;
		doProbeUpdate();
	}

	// Update distance to camera for all probes
	std::sort(mProbes.begin() + 1, mProbes.end(), CompareProbeDistance());
	llassert(mProbes[0] == mDefaultProbe && mProbes[0]->mCubeIndex == 0 &&
			 mProbes[0]->mCubeArray == mTexture);

	// Make sure we are assigning cube slots to the closest probes

	// First free any cube indices for distant probes
	for (U32 i = mReflectionProbeCount, count = mProbes.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep && probep->mCubeIndex != -1 && mUpdatingProbe != probep)
		{
			mCubeFree.push_back(probep->mCubeIndex);
			probep->mCubeArray = NULL;
			probep->mCubeIndex = -1;
			probep->mComplete = false;
			probep->mFadeIn = 0;
		}
	}

	// Next distribute the free indices
	for (U32 i = 1, count = llmin(mReflectionProbeCount, mProbes.size());
		 i < count && !mCubeFree.empty(); ++i)
	{
		// Find the closest probe that needs a cube index
		LLReflectionMap* probep = mProbes[i].get();
		if (probep && probep->mCubeIndex == -1)
		{
			S32 idx = allocateCubeIndex();
			if (!idx)	// This should not happen
			{
				llwarns << "Could not allocate a new cube index." << llendl;
				llassert(false);
			}
			probep->mCubeArray = mTexture;
			probep->mCubeIndex = idx;
		}
	}

	mResetFade = llmin(1.f, mResetFade + gFrameIntervalSeconds * 2.f);

	LLReflectionMap* closest_dynamicp = NULL;
	LLReflectionMap* oldest_probep = NULL;
	LLReflectionMap* oldest_occludedp = NULL;
	LLVector4a d;
	for (size_t i = 0, count = mProbes.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep->getNumRefs() == 1)
		{
			// No references held outside manager, delete this probe
			deleteProbe(i--);
			count = mProbes.size();
			continue;
		}

		if (probep != mDefaultProbe.get() &&
			(mPaused || !probep->isRelevant()))
		{
			// Skip irrelevant probes (or all non-default probes when paused).
			continue;
		}

		if (probep != mDefaultProbe.get())
		{
			LLViewerObject* objp = probep->mViewerObject.get();
			if (objp && !objp->isDead())
			{
				// Make sure probes track the object they are attached to.
				probep->mOrigin.load3(objp->getPositionAgent().mV);
			}
			d.setSub(camera_pos, probep->mOrigin);
			probep->mDistance = d.getLength3().getF32() - probep->mRadius;
		}
		else if (probep->mComplete)
		{
			// Set default probe at default distance for the purposes of
			// prioritization (if it has already been generated once).
			probep->mDistance = sDefaultDistance;
		}
		else
		{
			// Boost priority of default probe when it is not complete
			probep->mDistance = -4096.f;
		}

		if (probep->mComplete)
		{
			probep->autoAdjustOrigin();
			probep->mFadeIn = llmin(probep->mFadeIn + gFrameIntervalSeconds,
									1.f);
		}
		if (probep->mOccluded && probep->mComplete)
		{
			if (!oldest_occludedp)
			{
				oldest_occludedp = probep;
			}
			else if (probep->mLastUpdateTime <
						oldest_occludedp->mLastUpdateTime)
			{
				oldest_occludedp = probep;
			}
		}
		else if (!did_update && i < mReflectionProbeCount &&
				(!oldest_probep || check_priority(probep, oldest_probep)))
		{
		   oldest_probep = probep;
		}

		if (realtime && !closest_dynamicp && probep->mCubeIndex != -1 &&
			probep->getIsDynamic())
		{
			closest_dynamicp = probep;
		}

		if (level == 0)
		{
			// Only update default probe when coverage is set to none.
			break;
		}
	}

	if (realtime && closest_dynamicp)
	{
		// Update the closest dynamic probe realtime; should do a full
		// irradiance pass on "odd" frames and a radiance pass on "even" frames
		closest_dynamicp->autoAdjustOrigin();

		// Store and override the value of "isRadiancePass"; parts of the
		// render pipeline rely on "isRadiancePass" to set lighting values etc.
		bool radiance_pass = isRadiancePass();
		mRadiancePass = mRealtimeRadiancePass;
		for (U32 i = 0; i < 6; ++i)
		{
			updateProbeFace(closest_dynamicp, i);
		}
		mRealtimeRadiancePass = !mRealtimeRadiancePass;

		// Restore "isRadiancePass"
		mRadiancePass = radiance_pass;
	}

	static LLCachedControl<F32> upd_period(gSavedSettings,
										   "RenderDefaultProbeUpdatePeriod");
	F32 update_period = llclamp(F32(upd_period), 0.1, 10.0);
	if (gFrameTimeSeconds - mDefaultProbe->mLastUpdateTime < update_period)
	{
		if (!level)
		{
			// When probes are disabled do not update the default probe more
			// often than the prescribed update period.
			oldest_probep = NULL;
		}
	}
	else if (level)
	{
		// When probes are enabled do not update the default probe less often
		// than the prescribed update period.
		oldest_probep = mDefaultProbe.get();
	}

	// Switch to updating the next oldest probe
	if (!did_update && oldest_probep)
	{
		LLReflectionMap* probep = oldest_probep;
		llassert(probep->mCubeIndex != -1);
		probep->autoAdjustOrigin();
		++sUpdateCount;
		mUpdatingProbe = probep;
		doProbeUpdate();
	}

	if (oldest_occludedp)
	{
		// As far as this occluded probe is concerned, an origin/radius update
		// is as good as a full update.
		oldest_occludedp->autoAdjustOrigin();
		oldest_occludedp->mLastUpdateTime = gFrameTimeSeconds;
	}
}

LLReflectionMap* LLReflectionMapManager::addProbe(LLSpatialGroup* groupp)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	// Note: sReflectionProbesEnabled is false for OpenGL < 4.0f, but
	// apparently LL deems v4.0 as not good enough (thus the test for
	// mGLVersion here): is this normal ?  HB
	if (LLGLManager::sGLVersion < 4.1f ||
		!LLPipeline::sReflectionProbesEnabled)
	{
		return NULL;
	}

	LLReflectionMap* probep = new LLReflectionMap();
	probep->mGroup = groupp;

	if (mDefaultProbe.isNull())
	{
		// Safety check to make sure default probe is always first probe added
		mDefaultProbe = new LLReflectionMap();
		mProbes.push_back(mDefaultProbe);
	}
	llassert(mProbes[0] == mDefaultProbe);

	if (groupp)
	{
		probep->mOrigin = groupp->getOctreeNode()->getCenter();
	}

	if (gCubeSnapshot)
	{
		// Snapshot is in progress, mProbes is being iterated over: defer
		// insertion until next update.
		mCreateList.emplace_back(probep);
	}
	else
	{
		mProbes.emplace_back(probep);
	}

	return probep;
}

struct CompareProbeDepth
{
	bool operator()(const LLReflectionMap* lhs, const LLReflectionMap* rhs)
	{
		return lhs->mMinDepth < rhs->mMinDepth;
	}
};

void LLReflectionMapManager::getReflectionMaps(prmap_vec_t& maps)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	LLMatrix4a modelview = gGLModelView;
	LLVector4a oa;	// Scratch space for transformed origin

	U32 count = 0;
	U32 last_idx = 0;
	const U32 maps_size = maps.size();
	for (U32 i = 0, probes = mProbes.size(); i < probes && count < maps_size;
		 ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (!probep) continue;	// Paranoia ?

		// Something wants to use this probe, so let's indicate it has been
		// requested.
		probep->mLastBindTime = gFrameTimeSeconds;
		if (probep->mCubeIndex != -1)
		{
			if (!probep->mOccluded && probep->mComplete)
			{
				maps[count++] = probep;
				modelview.affineTransform(probep->mOrigin, oa);
				F32 radius = probep->mRadius;
				probep->mMinDepth = -oa.getF32ptr()[2] - radius;
				probep->mMaxDepth = -oa.getF32ptr()[2] + radius;
			}
		}
		else
		{
			probep->mProbeIndex = -1;
		}
		last_idx = i;
	}

	// Set remaining probe indices to -1
	for (U32 i = last_idx + 1, n = mProbes.size(); i < n; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep)	// Paranoia ?
		{
			probep->mProbeIndex = -1;
		}
	}

	if (count > 1)
	{
		std::sort(maps.begin(), maps.begin() + count, CompareProbeDepth());
	}

	for (U32 i = 0; i < count; ++i)
	{
		maps[i]->mProbeIndex = i;
	}

	// NULL-terminate list
	if (count < maps_size)
	{
		maps[count] = NULL;
	}
}

LLReflectionMap* LLReflectionMapManager::registerSpatialGroup(LLSpatialGroup* groupp)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (LLGLManager::sGLVersion < 4.1f)
	{
		return NULL;
	}

	if (!groupp)
	{
		return NULL;
	}
	LLSpatialPartition* partp = groupp->getSpatialPartition();
	if (!partp || partp->mPartitionType != LLViewerRegion::PARTITION_VOLUME)
	{
		return NULL;
	}

	OctreeNode* nodep = groupp->getOctreeNode();
	F32 size = nodep->getSize().getF32ptr()[0];
	if (size < 15.f || size > 17.f)
	{
		return NULL;
	}

	return addProbe(groupp);
}

LLReflectionMap* LLReflectionMapManager::registerViewerObject(LLViewerObject* vobjp)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!vobjp || vobjp->isDead() || !LLPipeline::sReflectionProbesEnabled)
	{
		return NULL;
	}

	LLReflectionMap* probep = new LLReflectionMap();
	probep->mViewerObject = vobjp;
	probep->mOrigin.load3(vobjp->getPositionAgent().mV);

	if (gCubeSnapshot)
	{
		// Snapshot is in progress, mProbes is being iterated over, defer
		// insertion until next update
		mCreateList.emplace_back(probep);
	}
	else
	{
		mProbes.emplace_back(probep);
	}

	return probep;
}

S32 LLReflectionMapManager::allocateCubeIndex()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (mCubeFree.empty())
	{
		return -1;
	}
	S32 ret = mCubeFree.front();
	mCubeFree.pop_front();
	return ret;
}

void LLReflectionMapManager::deleteProbe(U32 i)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	LLReflectionMap* probep = mProbes[i].get();
	if (probep == mDefaultProbe.get())
	{
		llwarns << "Attempt to remove the default probe. Aborted." << llendl;
		return;
	}

	if (probep->mCubeIndex != -1)
	{
		// Mark the cube index used by this probe as being free
		mCubeFree.push_back(probep->mCubeIndex);
	}
	if (mUpdatingProbe == probep)
	{
		mUpdatingProbe = NULL;
		mUpdatingFace = 0;
	}

	// Remove from any neighbors lists
	for (auto& otherp : probep->mNeighbors)
	{
		LLReflectionMap::reflmap_vec_t::iterator ne = otherp->mNeighbors.end();
		LLReflectionMap::reflmap_vec_t::iterator it =
			std::find(otherp->mNeighbors.begin(), ne, probep);
		if (it != ne)
		{
			otherp->mNeighbors.erase(it);
		}
	}

	mProbes.erase(mProbes.begin() + i);
}

void LLReflectionMapManager::doProbeUpdate()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!gUsePBRShaders)
	{
		return;
	}

	llassert(mUpdatingProbe != NULL);

	updateProbeFace(mUpdatingProbe, mUpdatingFace);

	if (++mUpdatingFace == 6)
	{
		updateNeighbors(mUpdatingProbe);
		mUpdatingFace = 0;
		if (isRadiancePass())
		{
			mUpdatingProbe->mComplete = true;
			mUpdatingProbe = NULL;
			mRadiancePass = false;
		}
		else
		{
			mRadiancePass = true;
		}
	}
}

class HBSavePipelineRT
{
public:
	LL_INLINE HBSavePipelineRT()
	:	mSavedRT(gPipeline.mRT)
	{
	}

	LL_INLINE ~HBSavePipelineRT()
	{
		gPipeline.mRT = mSavedRT;
	}

private:
	LLPipeline::RenderTargetPack* mSavedRT;
};

// Do the reflection map update render passes. For every 12 calls to this
// method, one complete reflection probe radiance map and irradiance map is
// generated. First six passes render the scene with direct lighting only into
// a scratch space cube map at the end of the cube map array and generate a
// simple mip chain (not convolution filter). At the end of these passes, an
// irradiance map is generated for this probe and placed into the irradiance
// cube map array at the index for this probe. The next six passes render the
// scene with both radiance and irradiance into the same scratch space cube map
// and generate a simple mip chain. At the end of these passes, a radiance map
// is generated for this probe and placed into the radiance cube map array at
// the index for this probe. In effect this simulates single-bounce lighting.
void LLReflectionMapManager::updateProbeFace(LLReflectionMap* probep, U32 face)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!gUsePBRShaders)
	{
		return;
	}

	mLightScale = 1.f;
	static LLCachedControl<F32> max_amb(gSavedSettings,
										"RenderReflectionProbeMaxAmbiance");
	if (!isRadiancePass() && probep->getAmbiance() > (F32)max_amb)
	{
		mLightScale = max_amb / probep->getAmbiance();
	}

	// Hacky hot-swap of camera specific render targets
	HBSavePipelineRT save_rt;
	gPipeline.mRT = &gPipeline.mAuxillaryRT;

	if (probep == mDefaultProbe.get())
	{
		touch_default_probe(probep);

		gPipeline.pushRenderTypeMask();

		// Only render sky, water, terrain, and clouds
		gPipeline.andRenderTypeMask(LLPipeline::RENDER_TYPE_SKY,
									LLPipeline::RENDER_TYPE_WL_SKY,
									LLPipeline::RENDER_TYPE_WATER,
									LLPipeline::RENDER_TYPE_VOIDWATER,
									LLPipeline::RENDER_TYPE_CLOUDS,
									LLPipeline::RENDER_TYPE_TERRAIN,
									LLPipeline::END_RENDER_TYPES);

		LLPipeline::sDefaultProbeRender = true;
		probep->update(mRenderTarget.getWidth(), face);
		LLPipeline::sDefaultProbeRender = false;

		gPipeline.popRenderTypeMask();
	}
	else
	{
		probep->update(mRenderTarget.getWidth(), face);
	}

	gPipeline.mRT = &gPipeline.mMainRT;

	S32 source_idx = mReflectionProbeCount;
	if (probep != mUpdatingProbe)
	{
		// This is the "realtime" probe that is updating every frame, use the
		// secondary scratch space channel
		++source_idx;
	}

	gGL.setColorMask(true, true);
	LLGLDepthTest depth(GL_FALSE, GL_FALSE);
	LLGLDisable cull(GL_CULL_FACE);
	LLGLDisable blend(GL_BLEND);

	// Downsample to placeholder map

	gGL.matrixMode(gGL.MM_MODELVIEW);
	gGL.pushMatrix();
	gGL.loadIdentity();

	gGL.matrixMode(gGL.MM_PROJECTION);
	gGL.pushMatrix();
	gGL.loadIdentity();

	gGL.flush();
	U32 res = mProbeResolution * 2;

	LLRenderTarget* screenp = &gPipeline.mAuxillaryRT.mScreen;

	// Perform a gaussian blur on the super sampled render before downsampling

	LLGLSLShader* shaderp = &gGaussianProgram;
	shaderp->bind();
	const F32 res_scale = 1.f / F32(mProbeResolution * 2);
	shaderp->uniform1f(sResScale, res_scale);
	S32 chan = shaderp->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE,
									  LLTexUnit::TT_TEXTURE);
	LLTexUnit* diffunitp = gGL.getTexUnit(chan);

	// Horizontal
	shaderp->uniform2f(sDirection, 1.f, 0.f);
	diffunitp->bind(screenp);
	mRenderTarget.bindTarget();
	gPipeline.mScreenTriangleVB->setBuffer();
	gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
	mRenderTarget.flush();

	// Vertical
	shaderp->uniform2f(sDirection, 0.f, 1.f);
	diffunitp->bind(&mRenderTarget);
	screenp->bindTarget();
	gPipeline.mScreenTriangleVB->setBuffer();
	gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
	screenp->flush();

	S32 mips = S32(log2f((F32)mProbeResolution) + 0.5f);

	shaderp = &gReflectionMipProgram;
	shaderp->bind();
	chan = shaderp->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE,
								  LLTexUnit::TT_TEXTURE);
	diffunitp = gGL.getTexUnit(chan);

	for (S32 i = 0, count = mMipChain.size(); i < count; ++i)
	{
		LLRenderTarget& target = mMipChain[i];
		target.bindTarget();
		diffunitp->bind(i ? &(mMipChain[i - 1]) : screenp);

		shaderp->uniform1f(sResScale, res_scale);

		gPipeline.mScreenTriangleVB->setBuffer();
		gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);

		res /= 2;

		S32 mip = i + mips - count;
		if (mip >= 0)
		{
			mTexture->bind(0);
			glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0,
								source_idx * 6 + face, 0, 0, res, res);
			mTexture->unbind();
		}
		target.flush();
	}

	gGL.popMatrix();
	gGL.matrixMode(gGL.MM_MODELVIEW);
	gGL.popMatrix();

	diffunitp->unbind(LLTexUnit::TT_TEXTURE);
	shaderp->unbind();

	if (face != 5)
	{
		return;	// We are done.
	}

	if (mMipChain.empty())	// Paranoia ?
	{
		llwarns_once << "mMipChain is empty !" << llendl;
		return;
	}

	if (!LLViewerShaderMgr::sHasIrrandiance)
	{
		// Cannot render this since the two gIrradianceGenProgram and
		// gRadianceGenProgram shaders have not loaded... HB
		return;
	}

	mMipChain[0].bindTarget();

	if (isRadiancePass())
	{
		// Generate radiance map (even if this is not the irradiance map, we
		// need the mip chain for the irradiance map).
		shaderp = &gRadianceGenProgram;
		shaderp->bind();

		mVertexBuffer->setBuffer();

		chan = shaderp->enableTexture(LLShaderMgr::REFLECTION_PROBES,
									  LLTexUnit::TT_CUBE_MAP_ARRAY);
		mTexture->bind(chan);
		shaderp->uniform1i(sSourceIdx, source_idx);
		shaderp->uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD,
						   mMaxProbeLOD);

		U32 res = mMipChain[0].getWidth();

		LLCoordFrame frame;
		F32 mat[16];
		for (size_t i = 0, count = mMipChain.size(); i < count; ++i)
		{
			shaderp->uniform1f(sRoughness, F32(i) / F32(count - 1));
			shaderp->uniform1f(LLShaderMgr::MIP_LEVEL, i);
			shaderp->uniform1i(sWidth, mProbeResolution);

			for (U32 cf = 0; cf < 6; ++cf)	// For each cube face
			{
				frame.lookAt(LLVector3::zero,
							 LLCubeMapArray::sClipToCubeLookVecs[cf],
							 LLCubeMapArray::sClipToCubeUpVecs[cf]);

				frame.getOpenGLRotation(mat);
				gGL.loadMatrix(mat);

				mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

				glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0,
									probep->mCubeIndex * 6 + cf, 0, 0,
									res, res);
			}

			if (i != count - 1)
			{
				res /= 2;
				glViewport(0, 0, res, res);
			}
		}
	}
	else
	{
		// Generate irradiance map
		shaderp = &gIrradianceGenProgram;
		shaderp->bind();
		chan = shaderp->enableTexture(LLShaderMgr::REFLECTION_PROBES,
									  LLTexUnit::TT_CUBE_MAP_ARRAY);
		mTexture->bind(chan);

		shaderp->uniform1i(sSourceIdx, source_idx);
		shaderp->uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD,
						   mMaxProbeLOD);

		mVertexBuffer->setBuffer();

		// Find the mip target to start with based on irradiance map resolution
		U32 start_mip = 0;
		U32 count = mMipChain.size();
		while (start_mip < count &&
			   mMipChain[start_mip].getWidth() != LL_IRRADIANCE_MAP_RESOLUTION)
		{
			++start_mip;
		}

		if (start_mip < count)
		{
			LLRenderTarget& target = mMipChain[start_mip];
			glViewport(0, 0, target.getWidth(), target.getHeight());

			F32 mat[16];
			for (U32 cf = 0; cf < 6; ++cf)	// For each cube face
			{
				LLCoordFrame frame;
				frame.lookAt(LLVector3::zero,
							 LLCubeMapArray::sClipToCubeLookVecs[cf],
							 LLCubeMapArray::sClipToCubeUpVecs[cf]);

				frame.getOpenGLRotation(mat);
				gGL.loadMatrix(mat);

				mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

				S32 res = target.getWidth();
				mIrradianceMaps->bind(chan);
				glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, 0, 0, 0,
									probep->mCubeIndex * 6 + cf, 0, 0,
									res, res);
				mTexture->bind(chan);
			}
		}
	}

	mMipChain[0].flush();

	shaderp->unbind();
}

void LLReflectionMapManager::pause(F32 duration)
{
	mPaused = true;
	mResumeTime = gFrameTimeSeconds + duration;
}

void LLReflectionMapManager::shift(const LLVector4a& offset)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	for (U32 i = 0, count = mProbes.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep)	// Paranoia
		{
			probep->mOrigin.add(offset);
		}
	}
}

void LLReflectionMapManager::updateNeighbors(LLReflectionMap* probep)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (mDefaultProbe.get() == probep)
	{
		return;
	}

	// Remove from existing neighbors
	for (auto& otherp : probep->mNeighbors)
	{
		LLReflectionMap::reflmap_vec_t::iterator ne = otherp->mNeighbors.end();
		LLReflectionMap::reflmap_vec_t::iterator it =
			std::find(otherp->mNeighbors.begin(), ne, probep);
		if (it != ne)
		{
			otherp->mNeighbors.erase(it);
		}
	}
	probep->mNeighbors.clear();

	// Search for new neighbors
	if (probep->isRelevant())
	{
		for (U32 i = 0, count = mProbes.size(); i < count; ++i)
		{
			LLReflectionMap* otherp = mProbes[i].get();
			if (otherp != mDefaultProbe.get() && otherp != probep)
			{
				if (otherp->isRelevant() && probep->intersects(otherp))
				{
					probep->mNeighbors.push_back(otherp);
					otherp->mNeighbors.push_back(probep);
				}
			}
		}
	}
}

void LLReflectionMapManager::updateUniforms()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!LLPipeline::sReflectionProbesEnabled || !mProbeData)
	{
		return;
	}

	mReflectionMaps.resize(mReflectionProbeCount);
	getReflectionMaps(mReflectionMaps);

	static F32 min_depth[256];
	for (U32 i = 0; i < 256; ++i)
	{
		mProbeData->refBucket[i][0] =
			mProbeData->refBucket[i][1] =
			mProbeData->refBucket[i][2] =
			mProbeData->refBucket[i][3] = mReflectionProbeCount;
		min_depth[i] = FLT_MAX;
	}

	LLMatrix4a modelview = gGLModelView;
	LLVector4a oa; // Scratch space for transformed origin

	S32 count = 0;
	// Neighbor "cursor": index into refNeighbor to start writing the next
	// probe's list of neighbors
	U32 nc = 0;

	static LLCachedControl<bool> auto_adjust(gSavedSettings,
											 "RenderSkyAutoAdjustLegacy");
	LLSettingsSky::ptr_t skyp = gEnvironment.getCurrentSky();
	F32 min_ambiance = skyp->getReflectionProbeAmbiance(auto_adjust);

	F32 ambscale, radscale;
	if (gCubeSnapshot && !isRadiancePass())	// Ambiance pass ?
	{
		ambscale = 0.f;
		radscale = 0.5f * llmax(mResetFade, 0.f);
	}
	else
	{
		ambscale = radscale = llmax(mResetFade, 0.f);
	}

	for (U32 k = 0, nmaps = mReflectionMaps.size(); k < nmaps; ++k)
	{
		LLReflectionMap* refmapp = mReflectionMaps[k].get();
		if (!refmapp)
		{
			break;
		}

		if (refmapp != mDefaultProbe.get())
		{
			// Bucket search data. Theory of operation:
			// 1. Determine minimum and maximum depth of each influence volume
			//	  and store in mDepth (done in getReflectionMaps).
			// 2. Sort by minimum depth.
			// 3. Prepare a bucket for each 1m of depth out to 256m.
			// 4. For each bucket, store the index of the nearest probe that
			//    might influence pixels in that bucket.
			// 5. In the shader, lookup the bucket for the pixel depth to get
			//    the index of the first probe that could possibly influence
			//    the current pixel.
			U32 depth_min = U32(llclamp(S32(refmapp->mMinDepth), 0, 255));
			U32 depth_max = U32(llclamp(S32(refmapp->mMaxDepth), 0, 255));
			for (U32 i = depth_min; i <= depth_max; ++i)
			{
				if (refmapp->mMinDepth < min_depth[i])
				{
					min_depth[i] = refmapp->mMinDepth;
					mProbeData->refBucket[i][0] = refmapp->mProbeIndex;
				}
			}
		}

		llassert(refmapp->mProbeIndex == count && refmapp->mCubeIndex >= 0 &&
				 mReflectionMaps[refmapp->mProbeIndex].get() == refmapp);
		LLViewerObject* objp = refmapp->mViewerObject.get();
		if (objp && !objp->isDead() && objp->getVolume())
		{
			// Have active manual probes live-track the object they are
			// associated with
			refmapp->mOrigin.load3(objp->getPositionAgent().mV);
			LLVOVolume* vobjp = objp->asVolume();
			if (vobjp && vobjp->getReflectionProbeIsBox())
			{
				static const LLVector3 half(0.5f, 0.5f, 0.5f);
				refmapp->mRadius = vobjp->getScale().scaledVec(half).length();
			}
			else
			{
				refmapp->mRadius = objp->getScale().mV[0] * 0.5f;
			}
		}
		modelview.affineTransform(refmapp->mOrigin, oa);
		mProbeData->refSphere[count].set(oa.getF32ptr());
		mProbeData->refSphere[count].mV[3] = refmapp->mRadius;

		mProbeData->refIndex[count][0] = refmapp->mCubeIndex;
		llassert(nc % 4 == 0);
		mProbeData->refIndex[count][1] = nc / 4;
		mProbeData->refIndex[count][3] = refmapp->mPriority;

		// For objects that are reflection probes, use the volume as the
		// influence volume of the probe only possibile influence volumes are
		// boxes and spheres, so detect boxes and treat everything else as
		// spheres
		if (refmapp->getBox(mProbeData->refBox[count]))
		{
			// Negate priority to indicate this probe has a box influence
			// volume
			mProbeData->refIndex[count][3] *= -1;
		}

		mProbeData->refParams[count].set(llmax(min_ambiance,
											   refmapp->getAmbiance()) * ambscale,
										 radscale,			// Radiance scale
										 refmapp->mFadeIn,	// Fade-in weight
										 // Z near
										 oa.getF32ptr()[2] - refmapp->mRadius);

		// Neighbor ("index"): index into refNeighbor to write indices for
		// current reflection probe's neighbors
		U32 ni = nc;
		// Pack neghbor list
		constexpr U32 MAX_NEIGHBORS = 64;
		U32 neighbor_count = 0;
		for (U32 n = 0, ncount = refmapp->mNeighbors.size();
			 n < ncount && ni < 4096 && neighbor_count < MAX_NEIGHBORS; ++n)
		{
			LLReflectionMap* neighborp = refmapp->mNeighbors[n];
			GLint idx = neighborp->mProbeIndex;
			if (idx != -1 && !neighborp->mOccluded &&
				neighborp->mCubeIndex != -1)
			{
				// This neighbor may be sampled
				mProbeData->refNeighbor[ni++] = idx;
				++neighbor_count;
			}
		}

		if (nc == ni)
		{
			// No neighbors, tag as empty
			mProbeData->refIndex[count][1] = -1;
		}
		else
		{
			mProbeData->refIndex[count][2] = ni - nc;

			// Move the cursor forward
			nc = ni;
			if (nc % 4 != 0)
			{
				// Jump to next power of 4 for compatibility with ivec4
				nc += 4 - (nc % 4);
			}
		}

		++count;
	}

	mProbeData->refmapCount = count;

	LLHeroProbeManager& hero = gPipeline.mHeroProbeManager;
	hero.updateUniforms();
	// Get the hero probe data
	mProbeData->heroBox = hero.mHeroData.heroBox;
	mProbeData->heroSphere = hero.mHeroData.heroSphere;
	mProbeData->heroShape = hero.mHeroData.heroShape;
	mProbeData->heroMipCount = hero.mHeroData.heroMipCount;
	mProbeData->heroProbeCount = hero.mHeroData.heroProbeCount;
	mProbeData->heroPlaneMatrix = hero.mHeroData.heroPlaneMatrix;

	// Copy rpd into uniform buffer object
	if (mUBO == 0)
	{
		glGenBuffers(1, &mUBO);
	}

	glBindBuffer(GL_UNIFORM_BUFFER, mUBO);
	glBufferData(GL_UNIFORM_BUFFER, sizeof(LLReflectionProbeData), mProbeData,
				 GL_STREAM_DRAW);
	glBindBuffer(GL_UNIFORM_BUFFER, 0);
}

void LLReflectionMapManager::setUniforms()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (LLPipeline::sReflectionProbesEnabled)
	{
		if (mUBO == 0)
		{
			updateUniforms();
		}
		glBindBufferBase(GL_UNIFORM_BUFFER, LLGLSLShader::UB_REFLECTION_PROBES,
						 mUBO);
	}
}

static void render_reflection_probe(LLReflectionMap* probep)
{
	if (!probep || !probep->isRelevant())
	{
		return;
	}

	F32* po = probep->mOrigin.getF32ptr();

	// Draw orange line from probe to neighbors
	gGL.flush();
	gGL.diffuseColor4f(1.f, 0.5f, 0.f, 1.f);
	gGL.begin(gGL.LINES);
	for (U32 i = 0, count = probep->mNeighbors.size(); i < count; ++i)
	{
		LLReflectionMap* neighborp = probep->mNeighbors[i];
		if (!neighborp) continue;	// Paranoia ?

		if (probep->mViewerObject.isNull() ||
			neighborp->mViewerObject.isNull())
		{
			gGL.vertex3fv(po);
			gGL.vertex3fv(neighborp->mOrigin.getF32ptr());
		}
	}
	gGL.end(true);

	gGL.diffuseColor4f(1.f, 1.f, 0.f, 1.f);
	gGL.begin(gGL.LINES);
	for (U32 i = 0, count = probep->mNeighbors.size(); i < count; ++i)
	{
		LLReflectionMap* neighborp = probep->mNeighbors[i];
		if (!neighborp) continue;	// Paranoia ?

		if (probep->mViewerObject.notNull() &&
			neighborp->mViewerObject.notNull())
		{
			gGL.vertex3fv(po);
			gGL.vertex3fv(neighborp->mOrigin.getF32ptr());
		}
	}
	gGL.end(true);
}

void LLReflectionMapManager::renderDebug()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	gDebugProgram.bind();
	for (size_t i = 0, count = mProbes.size(); i < count; ++i)
	{
		render_reflection_probe(mProbes[i].get());
	}
	gDebugProgram.unbind();
}

void LLReflectionMapManager::reset(bool hard)
{
	if (hard)
	{
		cleanup();
		sNeedsReregister = true;
		initReflectionMaps();
	}
	else
	{
		mReset = true;
	}
}

void LLReflectionMapManager::initReflectionMaps()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!gUsePBRShaders)
	{
		return;
	}

	static LLCachedControl<U32> qual(gSavedSettings,
									 "RenderReflectionProbeQuality");
	static LLCachedControl<F32> dist(gSavedSettings,
									 "RenderReflectionProbeDefaultDist");
	sDefaultDistance = llclamp((F32)dist, 32.f, 128.f);

	static LLCachedControl<U32> res(gSavedSettings,
									"RenderReflectionProbeResolution");
	U32 probe_res = nhpo2(llclamp((U32)res, 64, 512));
	if (mReset || mTexture.isNull() || mProbeResolution != probe_res ||
		mReflectionProbeCount != mDynamicProbeCount)
	{
		if (mProbeResolution != probe_res)
		{
			mRenderTarget.release();
			mMipChain.clear();
		}
#if LL_RESET_HDRI_SKY_ON_REFLECTION_MAP_RESET
		LLDrawPoolWLSky::resetHDRISky();
#endif
		mReset = false;
		mReflectionProbeCount = mDynamicProbeCount;
		mProbeResolution = probe_res;
		mMaxProbeLOD = log2f(mProbeResolution) - 1.f; // Number of mips - 1

		if (mTexture.isNull() ||
			mTexture->getResolution() != mProbeResolution ||
			mTexture->getCount() != mReflectionProbeCount + 2)
		{
			if (mTexture.isNull() || mIrradianceMaps.isNull())
			{
				U32 comps = qual ? 4 : 3;
				mTexture = new LLCubeMapArray();
				// Store mReflectionProbeCount + 2 cube maps, final two cube
				// maps are used for render target and radiance map generation
				// source).
				mTexture->allocate(mProbeResolution, comps,
								   mReflectionProbeCount + 2, true,
								   LLPipeline::sRenderHDR);

				mIrradianceMaps = new LLCubeMapArray();
				mIrradianceMaps->allocate(LL_IRRADIANCE_MAP_RESOLUTION, comps,
										  mReflectionProbeCount, false,
										  LLPipeline::sRenderHDR);
			}
			else
			{
				mTexture = new LLCubeMapArray(*mTexture, mProbeResolution,
											  mReflectionProbeCount + 2);
				mIrradianceMaps =
					new LLCubeMapArray(*mIrradianceMaps,
									   LL_IRRADIANCE_MAP_RESOLUTION,
									   mReflectionProbeCount);
			}
		}

		// Reset probe state
		mUpdatingFace = 0;
		mUpdatingProbe = NULL;
		mRadiancePass = mRealtimeRadiancePass = false;

		// If default probe already exists, remember whether or not it is
		// complete (SL-20498)
		bool default_complete = mDefaultProbe.notNull() &&
								mDefaultProbe->mComplete;
		for (U32 i = 0, count = mProbes.size(); i < count; ++i)
		{
			LLReflectionMap* probep = mProbes[i].get();
			if (probep)	// Paranoia
			{
				probep->mLastUpdateTime = 0.f;
				probep->mComplete = false;
				probep->mProbeIndex = -1;
				probep->mCubeArray = NULL;
				probep->mCubeIndex = -1;
				probep->mNeighbors.clear();
				probep->mFadeIn = 0;
			}
		}

		mCubeFree.clear();
		initCubeFree();

		if (mDefaultProbe.isNull())
		{
			// The default probe MUST be the first probe created
			llassert(mProbes.empty());
			mDefaultProbe = new LLReflectionMap();
			mProbes.push_back(mDefaultProbe);
		}

		llassert(mProbes[0] == mDefaultProbe);

		mDefaultProbe->mCubeIndex = 0;
		mDefaultProbe->mCubeArray = mTexture;
		mDefaultProbe->mDistance = sDefaultDistance;
		mDefaultProbe->mRadius = 4096.f;
		mDefaultProbe->mProbeIndex = 0;
		mDefaultProbe->mComplete = default_complete;
		touch_default_probe(mDefaultProbe);
	}

	U32 color_fmt;
	if (qual > 0)
	{
		color_fmt = LLPipeline::sRenderHDR ? GL_RGBA16F : GL_RGBA8;
	}
	else
	{
		color_fmt = LLPipeline::sRenderHDR ? GL_R11F_G11F_B10F : GL_RGB8;
	}

	if (!mRenderTarget.isComplete())
	{
		U32 tgt_res = mProbeResolution * 4; // Super sample
		mRenderTarget.allocate(tgt_res, tgt_res, color_fmt, true);
	}

	if (mMipChain.empty())
	{
		U32 res = mProbeResolution;
		U32 count = U32(log2f(F32(res)) + 0.5f);

		mMipChain.resize(count);
		for (U32 i = 0; i < count; ++i)
		{
			mMipChain[i].allocate(res, res, color_fmt);
			res /= 2;
		}
	}

	if (mVertexBuffer.isNull())
	{
		constexpr U32 mask = LLVertexBuffer::MAP_VERTEX;
		mVertexBuffer = new LLVertexBuffer(mask);
		mVertexBuffer->allocateBuffer(4, 0);

		LLStrider<LLVector3> v;

		mVertexBuffer->getVertexStrider(v);

		v[0] = LLVector3(-1.f, -1.f, -1.f);
		v[1] = LLVector3(1.f, -1.f, -1.f);
		v[2] = LLVector3(-1.f, 1.f, -1.f);
		v[3] = LLVector3(1.f, 1.f, -1.f);

		mVertexBuffer->unmapBuffer();
	}
}

void LLReflectionMapManager::cleanup()
{
	mVertexBuffer = NULL;
	mRenderTarget.release();

	mMipChain.clear();

	mTexture = NULL;
	mIrradianceMaps = NULL;

	mReflectionProbeCount = 0;
	mProbes.clear();
	mKillList.clear();
	mCreateList.clear();

	mReflectionMaps.clear();
	mUpdatingFace = 0;

	mDefaultProbe = NULL;
	mUpdatingProbe = NULL;

	LLGLManager::deleteBuffers(1, &mUBO);
	mUBO = 0;

	cleanupQueryPool();

	// Note: also called on teleport (not just shutdown), so make sure we are
	// in a good "starting" state.
	mCubeFree.clear();
	initCubeFree();
}

void LLReflectionMapManager::cleanupQueryPool()
{
	if (mQueryPool.empty())
	{
		return;
	}
	std::vector<U32> queries(mQueryPool.begin(), mQueryPool.end());
	mQueryPool.clear();
	glDeleteQueries((S32)queries.size(), queries.data());
}

U32 LLReflectionMapManager::allocateQuery()
{
	if (mQueryPool.empty())
	{
		U32 query;
		glGenQueries(1, &query);
		return query;
	}

	U32 query = mQueryPool.front();
	mQueryPool.pop_front();
	return query;
}

void LLReflectionMapManager::recycleQuery(U32 query)
{
	// To avoid VRAM over-usage, do not let the pool grow beyond a reasonnable
	// size (seen happening for example when several regions disconnect after
	// you cammed into them and then reset the camera, their probes being
	// destroyed and the corresponding slots not going to be reused any time
	// soon). HB
	constexpr size_t MAX_POOL_SIZE = 2 * LL_MAX_REFLECTION_PROBE_COUNT;
	if (mQueryPool.size() >= MAX_POOL_SIZE)
	{
		LL_DEBUGS_SPARSE("Probes") << "Occlusion query pool is full."
								   << LL_ENDL;
		glDeleteQueries(1, &query);
		return;
	}
	mQueryPool.push_back(query);
}

void LLReflectionMapManager::doOcclusion()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!gUsePBRShaders)
	{
		return;
	}

	LLVector4a eye;
	eye.load3(gViewerCamera.getOrigin().mV);

	for (size_t i = 0, count = mProbes.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep && probep != mDefaultProbe.get())
		{
			probep->doOcclusion(eye);
		}
	}
}

void LLReflectionMapManager::forceDefaultProbeAndUpdateUniforms(bool force)
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	static std::vector<bool> was_occluded;
	if (force)
	{
		was_occluded.clear();
		for (U32 i = 0, count = mProbes.size(); i < count; ++i)
		{
			LLReflectionMap* probep = mProbes[i].get();
			was_occluded.push_back(probep && probep->mOccluded);
			if (probep && probep != mDefaultProbe.get())
			{
				probep->mOccluded = true;
			}
		}
		updateUniforms();
	}
	else
	{
		for (U32 i = 0, count = llmin(mProbes.size(), was_occluded.size());
			 i < count; ++i)
		{
			LLReflectionMap* probep = mProbes[i].get();
			was_occluded.push_back(probep && probep->mOccluded);
			if (probep)
			{
				probep->mOccluded = was_occluded[i];
			}
		}
		was_occluded.clear();
	}
}

///////////////////////////////////////////////////////////////////////////////
// LLHeroProbeManager class
///////////////////////////////////////////////////////////////////////////////

//static
bool LLHeroProbeManager::sNeedsReregister = false;

LLHeroProbeManager::LLHeroProbeManager()
:	mReflectionProbeCount(0),
	mLastHeroRenderTime(0.f),
	mProbeResolution(1024),
	mMaxProbeLOD(6.f),
	mNearestProbeDist(F32_MAX),
	mMirrorNormal(0.f, 0.f, 1.f),
	mIsPlanar(false),
	mReset(false),
	mRenderingMirror(false),
	mHeroShadowsComplete(false)
{
}

LLHeroProbeManager::~LLHeroProbeManager()
{
	cleanup();
	mHeroVOList.clear();
	mNearestHero = NULL;
}

void LLHeroProbeManager::cleanup()
{
	mVertexBuffer = NULL;
	mRenderTarget.release();
	mMipChain.clear();
	mTexture = NULL;
	mReflectionProbeCount = 0;
	mProbes.clear();
	mDefaultProbe = NULL;
}

void LLHeroProbeManager::initReflectionMaps()
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	if (!LLPipeline::sRenderMirrors)
	{
		return;
	}

	if (mReset)
	{
		mReset = false;
		cleanup();
		sNeedsReregister = true;
	}

	static LLCachedControl<U32> max_count(gSavedSettings,
										  "RenderHeroProbesMaxCount");
	const U32 count = llclamp(U32(max_count), 1, LL_MAX_HERO_PROBE_COUNT);
	if (mTexture.isNull() || mReflectionProbeCount != count)
	{
		mReflectionProbeCount = count;
		static LLCachedControl<U32> probe_level(gSavedSettings,
												"RenderHeroProbeResLevel");
		switch ((U32)probe_level)
		{
			// Note: 0 now actually means hero probe/mirror disabled (256x256
			// is way too small and blurry for a mirror, anyway). HB
			case 0:		mProbeResolution = 256; break;
			case 1:		mProbeResolution = 512; break;
			case 2:		mProbeResolution = 1024; break;
			default:	mProbeResolution = 2048; break;
		}
		// Number of mips - 1
		mMaxProbeLOD = log2f(F32(mProbeResolution)) - 1.f;

		mTexture = new LLCubeMapArray();
		// We use two more cube maps for render target and radiance map
		// generation source.
		mTexture->allocate(mProbeResolution, 3, mReflectionProbeCount + 2,
						   true, LLPipeline::sRenderHDR);

		if (mDefaultProbe.isNull())
		{
			// The default probe MUST be the first probe created
			llassert_always(mProbes.empty());
			mDefaultProbe = new LLReflectionMap();
			mProbes.push_back(mDefaultProbe);
		}
		llassert(mProbes[0] == mDefaultProbe);

		// For hero probes, we treat this as the main mirror probe.
		mDefaultProbe->mCubeIndex = 0;
		mDefaultProbe->mCubeArray = mTexture;
		static LLCachedControl<U32> probe_dist(gSavedSettings,
											   "RenderHeroProbeDistance");
		mDefaultProbe->mDistance = llclamp((F32)probe_dist, 2.f, 64.f);
		mDefaultProbe->mRadius = 4096.f;
		mDefaultProbe->mProbeIndex = 0;
		touch_default_probe(mDefaultProbe);
		mProbes.push_back(mDefaultProbe);
	}

	if (mVertexBuffer.isNull())
	{
		constexpr U32 mask = LLVertexBuffer::MAP_VERTEX;
		mVertexBuffer = new LLVertexBuffer(mask);
		mVertexBuffer->allocateBuffer(4, 0);

		LLStrider<LLVector3> v;

		mVertexBuffer->getVertexStrider(v);

		v[0] = LLVector3(-1.f, -1.f, -1.f);
		v[1] = LLVector3(1.f, -1.f, -1.f);
		v[2] = LLVector3(-1.f, 1.f, -1.f);
		v[3] = LLVector3(1.f, 1.f, -1.f);

		mVertexBuffer->unmapBuffer();
	}
}

void LLHeroProbeManager::update()
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	mNearestHero = NULL;
	mNearestProbeDist = F32_MAX;

	if (!LLPipeline::sRenderMirrors || gDisconnected ||
		!LLStartUp::isLoggedIn() || gAppViewerp->logoutRequestSent())
	{
		return;
	}

	initReflectionMaps();

	// Probe 0 is always our mirror probe.
	if (mProbes[0].isNull())
	{
		llwarns_sparse << "NULL default hero probe." << llendl;
		return;
	}
	llassert(mProbes[0] == mDefaultProbe);

	U32 color_fmt = LLPipeline::sRenderHDR ? GL_RGBA16F : GL_RGBA8;

	if (!mRenderTarget.isComplete())
	{
		mRenderTarget.allocate(mProbeResolution, mProbeResolution, color_fmt,
							   true);
		llinfos << "Allocated a render target for mirrors at size: "
				<< mProbeResolution << "x" << mProbeResolution << llendl;
	}

	if (mMipChain.empty())
	{
		U32 res = mProbeResolution;
		U32 count = U32(log2f(F32(res)) + 0.5f);
		mMipChain.resize(count);
		for (U32 i = 0; i < count; ++i)
		{
			mMipChain[i].allocate(res, res, color_fmt);
			res /= 2;
		}
		llinfos << "Allocated " << count << " mips for mirrors." << llendl;
	}

	F32 far_cam_dist = gViewerCamera.getFar();
	const LLVector3& camera_pos = gViewerCamera.getOrigin();

	std::vector<LLPointer<LLVOVolume> > dead;
	// Find the closest mirror, if any.
	LLVector4a center, size;
	LLVector3 normal;
	for (volp_set_t::iterator it = mHeroVOList.begin(),
							  end = mHeroVOList.end();
		 it != end; ++it)
	{
		LLVOVolume* volp = it->get();
		if (!volp || volp->isDead())
		{
			dead.push_back(volp);
			continue;
		}
		if (volp->mDrawable.isNull() || !volp->isReflectionProbe() ||
			!volp->getReflectionProbeIsBox())
		{
			// Probe object still rezzing ?  HB
			continue;
		}

		const LLVector3& vol_pos = volp->getPositionAgent();
		LLVector3 offset = camera_pos - vol_pos;
		F32 distance = offset.length();
		if (distance > far_cam_dist || distance > mNearestProbeDist)
		{
			continue;
		}

		center.load3(vol_pos.mV);
		size.load3(volp->getScale().mV);
		if (gViewerCamera.AABBInFrustum(center, size))
		{
			// Check to see if the camera is in front of the +Z face of the
			// hero probe (if it is behind, then the probe is not interesting).
			normal.set(0.f, 0.f, 1.f);
			normal *= volp->mDrawable->getWorldRotation();
			normal.normalize();
			if (normal * offset >= 0.f)
			{
				// Yes, this probe is a candidate
				mNearestProbeDist = distance;
				mNearestHero = volp;
				mMirrorNormal = normal;
			}
		}
	}

	for (U32 i = 0, count = dead.size(); i < count; ++i)
	{
		unregisterViewerObject(dead[i].get());
	}

	// No close, visible and valid/live hero probe. Abort.
	if (mNearestHero.isNull() || mNearestHero->isDead() ||
		mNearestHero->mDrawable.isNull())
	{
		// Set the tracked object for this probe to NULL.
		mProbes[0]->mViewerObject = NULL;
		mNearestHero = NULL;
		mNearestProbeDist = F32_MAX;
		return;
	}
	// Update the tracked object for this probe.
	mProbes[0]->mViewerObject = mNearestHero;

	mMirrorPosition = mNearestHero->getPositionAgent();
#if 0	// Already done above (mMirrorNormal = normal). HB
	mMirrorNormal.set(0.f, 0.f, 1.f);
	mMirrorNormal *= mNearestHero->mDrawable->getWorldRotation();
	mMirrorNormal.normalize();
#endif
	mCurrentClipPlane.setVec(mMirrorPosition, mMirrorNormal);

	if (mIsPlanar)
	{
		const LLVector3& fwd = gViewerCamera.getAtAxis();
		const LLVector3& up = gViewerCamera.getUpAxis();
		mPlanarLookDir = fwd - 2.f * (fwd * normal) * normal;
		mPlanarLookDir.normalize();
		mPlanarUpDir = up - 2.f * (up * normal) * normal;
		mPlanarUpDir.normalize();
	}

	LLVector3 offset = camera_pos - mMirrorPosition;
	LLVector3 project = mMirrorNormal * (offset * mMirrorNormal);
	LLVector3 reject = offset - project;
	LLVector3 point = (reject - project) + mMirrorPosition;
	mProbes[0]->mOrigin.load3(point.mV);
	mProbes[0]->mRadius = mNearestHero->getScale().length() * 0.5f;
}

void LLHeroProbeManager::renderProbes()
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	if (mNearestHero.isNull() || mProbes.empty() || mProbes[0].isNull() ||
		!isAgentAvatarValid())
	{
		return;	// Nothing to do.
	}

	static LLCachedControl<U32> fps(gSavedSettings, "RenderHeroProbeMaxFPS");
	F32 delta = gFrameTimeSeconds - mLastHeroRenderTime;
	if (delta < 1.f / llclamp((F32)fps, 1.f, 60.f))
	{
		return;
	}
	mLastHeroRenderTime = gFrameTimeSeconds;

	// Do not update mips when the nearest probe is beyond a distance from the
	// camera determined by the probe mDistance and a user-configurable cut-off
	// factor. This avoids excessive GPU consumption and lowered FPS rates when
	// there is no mirror close to the camera, or even no mirror currently
	// rendered at all !  HB
	static LLCachedControl<F32> cutoff(gSavedSettings,
									   "RenderHeroProbeCutoff");
	if (cutoff > 1.f && mNearestProbeDist > mProbes[0]->mDistance * cutoff)
	{
		mProbes[0]->autoAdjustOrigin();	// Still adjust the position...
		return;
	}

	bool restore_first_person = !LLVOAvatar::sVisibleInFirstPerson;
	U32 camera_mode = gAgent.getCameraMode();
	gAgentAvatarp->updateAttachmentVisibility(camera_mode);

	bool radiance_pass = gPipeline.mReflectionMapManager.isRadiancePass();
	gPipeline.mReflectionMapManager.mRadiancePass = true;
	mRenderingMirror = true;
	mHeroShadowsComplete = false;
	if (!mProbes[0]->mOccluded)
	{
		constexpr F32 NEAR_CLIP = 0.01f;
		bool is_dynamic = mNearestHero->getReflectionProbeIsDynamic();
		if (mIsPlanar)
		{
			updateProbeFace(mProbes[0], 0, is_dynamic, NEAR_CLIP);
		}
		else
		{
			for (U32 i = 0; i < 6; ++i)
			{
				updateProbeFace(mProbes[0], i, is_dynamic, NEAR_CLIP);
			}
		}
		generateRadiance(mProbes[0]);
	}
	mRenderingMirror = false;

	if (restore_first_person)
	{
		LLVOAvatar::sVisibleInFirstPerson = false;
	}
	// Must run after mRenderingMirror = false and the flag restore
	gAgentAvatarp->updateAttachmentVisibility(camera_mode);

	gPipeline.mReflectionMapManager.mRadiancePass = radiance_pass;

	mProbes[0]->autoAdjustOrigin();
}

// Does the reflection map update render passes. For every 12 calls of this
// method, one complete reflection probe radiance map and irradiance map is
// generated. First 6 passes render the scene with direct lighting only into a
// scratch space cube map at the end of the cube a simple mip chain (not
// convolution filter). At the end of these passes, an irradiance map is
// generated for this probe and placed into the irradiance cube map array at
// the index for this probe. The next six passes render the scene with both
// radiance and irradiance into the same scratch space cube map and generate
// a simple mip chain. Finally, a radiance map is generated for this probe and
// placed into the radiance cube map array at the index for this probe. In
// effect this simulates single-bounce lighting.
void LLHeroProbeManager::updateProbeFace(LLReflectionMap* probep, U32 face,
										 bool is_dynamic, F32 near_clip)
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	// Hacky hot-swap of camera specific render targets
	HBSavePipelineRT save_rt;
	gPipeline.mRT = &gPipeline.mHeroProbeRT;
	if (mIsPlanar)
	{
		probep->update(mRenderTarget.getWidth(), face, is_dynamic, near_clip,
					   true, mCurrentClipPlane, &mPlanarLookDir,
					   &mPlanarUpDir);
	}
	else
	{
		probep->update(mRenderTarget.getWidth(), face, is_dynamic, near_clip);
	}
	gPipeline.mRT = &gPipeline.mMainRT;

	gGL.setColorMask(true, true);
	LLGLDepthTest depth(GL_FALSE, GL_FALSE);
	LLGLDisable cull(GL_CULL_FACE);
	LLGLDisable blend(GL_BLEND);

	// Downsample to placeholder map

	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.pushMatrix();
	gGL.loadIdentity();

	gGL.matrixMode(LLRender::MM_PROJECTION);
	gGL.pushMatrix();
	gGL.loadIdentity();

	gGL.flush();

	LLRenderTarget* screenp = &gPipeline.mHeroProbeRT.mScreen;

	// Perform a gaussian blur on the super sampled render before downsampling
	LLGLSLShader* shaderp = &gGaussianProgram;
	shaderp->bind();
	shaderp->uniform1f(sResScale, 0.5f / F32(mProbeResolution));
	S32 chan = shaderp->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE,
									  LLTexUnit::TT_TEXTURE);
	LLTexUnit* diffunitp = gGL.getTexUnit(chan);
	// Horizontal
	shaderp->uniform2f(sDirection, 1.f, 0.f);
	diffunitp->bind(screenp);
	mRenderTarget.bindTarget();
	gPipeline.mScreenTriangleVB->setBuffer();
	gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
	mRenderTarget.flush();
	// Vertical
	shaderp->uniform2f(sDirection, 0.f, 1.f);
	diffunitp->bind(&mRenderTarget);
	screenp->bindTarget();
	gPipeline.mScreenTriangleVB->setBuffer();
	gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
	screenp->flush();
	shaderp->unbind();

	// Unlike the reflectionmap manager, all probes are considered "realtime"
	// for hero probes.
	U32 src_idx = mReflectionProbeCount + 1;
	S32 mips = S32(log2f((F32)mProbeResolution) + 0.5f);
	shaderp = &gReflectionMipProgram;
	shaderp->bind();

	LLRenderTarget* depthp = &gPipeline.mHeroProbeRT.mDeferredScreen;
	chan = shaderp->enableTexture(LLShaderMgr::DEFERRED_DIFFUSE,
								  LLTexUnit::TT_TEXTURE);
	diffunitp = gGL.getTexUnit(chan);

	// Note: there is no (no more ?) depth channel texture in the
	// gReflectionMipProgram shader !  HB
	chan = shaderp->enableTexture(LLShaderMgr::DEFERRED_DEPTH,
								  LLTexUnit::TT_TEXTURE);
	LLTexUnit* depthunitp = chan < 0 ? NULL : gGL.getTexUnit(chan);

	U32 res = mProbeResolution;
	for (S32 i = 0, count = mMipChain.size(); i < count; ++i)
	{
		mMipChain[i].bindTarget();
		diffunitp->bind(i ? &(mMipChain[i - 1]) : screenp);
		if (depthunitp)
		{
			depthunitp->bind(depthp, true);
		}
		shaderp->uniform1f(sResScale, 0.5f / F32(mProbeResolution));
		shaderp->uniform1f(sZnear, probep->getNearClip());
		shaderp->uniform1f(sZfar, MAX_FAR_CLIP);
		gPipeline.mScreenTriangleVB->setBuffer();
		gPipeline.mScreenTriangleVB->drawArrays(LLRender::TRIANGLES, 0, 3);
		S32 mip = i - count + mips;
		if (mip >= 0)
		{
			mTexture->bind(0);
			glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, mip, 0, 0,
								src_idx * 6 + face, 0, 0, res, res);
			mTexture->unbind();
		}
		res /= 2;
		mMipChain[i].flush();
	}

	gGL.popMatrix();
	gGL.matrixMode(LLRender::MM_MODELVIEW);
	gGL.popMatrix();

	diffunitp->unbind(LLTexUnit::TT_TEXTURE);
	shaderp->unbind();
}

// Radiance generation is done in a separate stage; this is to better enable
// independent control over how we generate radiance vs. having it coupled with
// processing the final face of the probe. Useful when we may not always be
// rendering a full set of faces of the probe.
void LLHeroProbeManager::generateRadiance(LLReflectionMap* probep)
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	if (!LLViewerShaderMgr::sHasIrrandiance)
	{
		return;
	}

	// Unlike the reflectionmap manager, all probes are considered "realtime"
	// for hero probes.
	U32 src_idx = mReflectionProbeCount + 1;
	mMipChain[0].bindTarget();

	// Generate radiance map (even if this is not the irradiance map, we need
	// the mip chain for the irradiance map).
	LLGLSLShader& shader = gHeroRadianceGenProgram;
	shader.bind();
	mVertexBuffer->setBuffer();

	S32 chan = shader.enableTexture(LLShaderMgr::REFLECTION_PROBES,
									LLTexUnit::TT_CUBE_MAP_ARRAY);
	mTexture->bind(chan);
	shader.uniform1i(sSourceIdx, src_idx);
	shader.uniform1f(LLShaderMgr::REFLECTION_PROBE_MAX_LOD, mMaxProbeLOD);
	U32 res = mMipChain[0].getWidth();
	LLCoordFrame frame;
	F32 mat[16];
	size_t mips_chain_size = mMipChain.size();
	F32 roughness_factor = 1.f / F32(mips_chain_size - 1);
	for (size_t i = 0, count = mips_chain_size / 4; i < count; ++i)
	{
		shader.uniform1f(sRoughness, (F32)i * roughness_factor);
		shader.uniform1f(LLShaderMgr::MIP_LEVEL, (F32)i);
		shader.uniform1i(sWidth, mProbeResolution);
		// For each cube face
		for (U32 cf = 0; cf < 6; ++cf)
		{
			frame.lookAt(LLVector3::zero,
						 LLCubeMapArray::sClipToCubeLookVecs[cf],
						 LLCubeMapArray::sClipToCubeUpVecs[cf]);

			frame.getOpenGLRotation(mat);
			gGL.loadMatrix(mat);

			mVertexBuffer->drawArrays(gGL.TRIANGLE_STRIP, 0, 4);

			glCopyTexSubImage3D(GL_TEXTURE_CUBE_MAP_ARRAY, i, 0, 0,
								probep->mCubeIndex * 6 + cf, 0, 0,
								res, res);
		}

		if (i != count - 1)
		{
			res /= 2;
			glViewport(0, 0, res, res);
		}
	}

	shader.unbind();
	mMipChain[0].flush();
}

void LLHeroProbeManager::updateUniforms()
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	if (!LLPipeline::sRenderMirrors)
	{
		mHeroData.heroProbeCount = 0;
		return;
	}

	// Captures are camera-locked: never sample one hero while rendering
	// another (gCubeSnapshot).
	mHeroData.heroProbeCount = gCubeSnapshot ? 0 : 1;

	if (mNearestHero.notNull() && !mNearestHero->isDead() &&
		mDefaultProbe.notNull())
	{
		LLMatrix4a modelview = gGLModelView;
		LLVector4a oa;	// Scratch space for transformed origin
		oa.set(0.f, 0.f, 0.f, 0.f);
		F32 radius;
		if (mNearestHero->getReflectionProbeIsBox())
		{
			static const LLVector3 half(0.5f, 0.5f, 0.5f);
			radius = mNearestHero->getScale().scaledVec(half).length();
		}
		else
		{
			radius = mNearestHero->getScale().mV[0] * 0.5f;
		}
		mDefaultProbe->mRadius = radius;
		modelview.affineTransform(mDefaultProbe->mOrigin, oa);
		mHeroData.heroSphere.set(oa.getF32ptr());
		mHeroData.heroSphere.mV[3] = radius;
		mHeroData.heroShape = mDefaultProbe->getBox(mHeroData.heroBox) ? 0 : 1;

		if (mIsPlanar)
		{
			mHeroData.heroShape = 2;
			LLVector3 relf_right = mPlanarLookDir % mPlanarUpDir;
			relf_right.normalize();

			// Build rotation mapping world directions to face 0 cubemap space.
			// initRows sets mMatrix[i] which becomes GLSL column i via std140,
			// so each "row" argument here is actually a GLSL column.
			LLVector4 r0(mPlanarLookDir.mV[VX], -mPlanarUpDir.mV[VX],
						 -relf_right.mV[VX], 0.f);
			LLVector4 r1(mPlanarLookDir.mV[VY], -mPlanarUpDir.mV[VY],
						 -relf_right.mV[VY], 0.f);
			LLVector4 r2(mPlanarLookDir.mV[VZ], -mPlanarUpDir.mV[VZ],
						 -relf_right.mV[VZ], 0.f);
			mHeroData.heroPlaneMatrix.initRows(r0, r1, r2, LLVector4());
		}
	}

	mHeroData.heroMipCount = mMipChain.size();
}

void LLHeroProbeManager::renderDebug()
{
	LL_TRACY_TIMER(TRC_HERO_PROBE);

	gDebugProgram.bind();
	for (size_t i = 0, count = mProbes.size(); i < count; ++i)
	{
		render_reflection_probe(mProbes[i].get());
	}
	gDebugProgram.unbind();
}

void LLHeroProbeManager::doOcclusion()
{
	LL_TRACY_TIMER(TRC_REFLECTION_PROBE);

	if (!gUsePBRShaders)
	{
		return;
	}

	LLVector4a eye;
	eye.load3(gViewerCamera.getOrigin().mV);

	for (size_t i = 0, count = mProbes.size(); i < count; ++i)
	{
		LLReflectionMap* probep = mProbes[i].get();
		if (probep && probep != mDefaultProbe.get())
		{
			probep->doOcclusion(eye);
		}
	}
}

bool LLHeroProbeManager::registerViewerObject(LLVOVolume* volp)
{
	if (!volp || volp->isDead() || volp->mDrawable.isNull())
	{
		return false;	// Nope, we did not register this !  HB
	}
	mHeroVOList.emplace(volp);
	if (volp)
	{
		volp->mIsHeroProbe = true;
	}
	return true;		// It is now indeed in our list. HB
}

void LLHeroProbeManager::unregisterViewerObject(LLVOVolume* volp)
{
	mHeroVOList.erase(volp);
	if (volp)
	{
		volp->mIsHeroProbe = false;
	}
}
