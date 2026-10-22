/**
 * @file ndconvexdecomphacd.h
 *
 * $LicenseInfo:firstyear=2011&license=viewergpl$
 *
 * Copyright (c) 2011, Nicky Dasmijn <sl.nicky.ml@googlemail.com>
 * Copyright (c) 2026, Henri Beauchamp
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

#include <map>
#include <vector>

#include "llconvexdecomposition.h"

#define HACD_NUM_STAGES 1

class HACDDecoder;

class NDConvexDecompHACD final : public LLConvexDecomposition
{
protected:
	LOG_CLASS(NDConvexDecompHACD);

public:
	NDConvexDecompHACD();

	// Generate a decomposition object handle
	void genDecomposition(S32& decomp) override;
	// Delete decomposition object handle
	void deleteDecomposition(S32 decomp) override;
	// Bind given decomposition handle
	// Commands operate on currently bound decomposition
	void bindDecomposition(S32 decomp) override;

	// Sets *params_outp to the address of the LLCDParam array and returns
	// the length of the array
	S32 getParameters(const LLCDParam** params_outp) override;
	S32 getStages(const LLCDStageData** stages_outp) override;

	// Set a parameter by name. Returns false if out of bounds or unsupported
	// parameter
	LLCDResult setParam(const char* name, F32 val) override;
	LLCDResult setParam(const char* name, S32 val) override;
	LLCDResult setParam(const char* name, bool val) override;

	LLCDResult setMeshData(const LLCDMeshData* datap,
						   bool vertex_based) override;
	LLCDResult registerCallback(S32 stage, llcd_callback_t callback) override;

	LLCDResult executeStage(S32 stage) override;
	LLCDResult buildSingleHull() override;

	S32 getNumHullsFromStage(S32 stage) override;

	LLCDResult getHullFromStage(S32 stage, S32 hull,
								LLCDHull* hulloutp) override;
	LLCDResult getSingleHull(LLCDHull* hulloutp) override;

	// *TODO: Implement lock of some kind to disallow this call if data not yet
	// ready
	LLCDResult getMeshFromStage(S32 stage, S32 hull,
								LLCDMeshData* dataoutp) override;
	LLCDResult getMeshFromHull(LLCDHull* hullinp,
							   LLCDMeshData* meshoutp) override;

	// For visualizing convex hull shapes in the viewer physics shape display
	LLCDResult generateSingleHullMeshFromMesh(LLCDMeshData* meshinp,
											  LLCDMeshData* meshoutp) override;

private:
	HACDDecoder*					mSingleHullMeshFromMesh;
	std::map<S32, HACDDecoder*>		mDecoders;
	S32								mCurrentDecoder;
	S32								mNextId;

	std::vector<F32>				mMeshToHullVertices;
	std::vector<S32>				mMeshToHullTriangles;

	static LLCDStageData			mStages[HACD_NUM_STAGES];
	static LLCDParam				mParams[4];
	static LLCDParam::LLCDEnumItem	mMethods[HACD_NUM_STAGES];
	static LLCDParam::LLCDEnumItem	mQuality[HACD_NUM_STAGES];
	static LLCDParam::LLCDEnumItem	mSimplify[HACD_NUM_STAGES];
};
