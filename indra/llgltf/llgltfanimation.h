/**
 * @file llgltfanimation.h
 * @brief LLGLTF Implementation
 *
 * $LicenseInfo:firstyear=2024&license=viewergpl$
 *
 * Copyright (c) 2024, Linden Research, Inc.
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

#include "llgltfaccessor.h"
#include "llgltfglm.h"
#include "llmemory.h"			// For LL_ALIGNED16_NEW_DELETE

namespace LLGLTF
{
	class Asset;

	class alignas(16) Animation
	{
	protected:
		LOG_CLASS(LLGLTF::Animation);

	public:
		LL_ALIGNED16_NEW_DELETE

		LL_INLINE Animation()
		:	mMinTime(0.f),
			mMaxTime(0.f),
			mTime(0.f)
		{
		}

		class Sampler
		{
		protected:
			LOG_CLASS(LLGLTF::Animation::Sampler);

		public:
			LL_INLINE Sampler()
			:	mMinTime(-F32_MAX),
				mMaxTime(F32_MAX),
				mInput(INVALID_INDEX),
				mOutput(INVALID_INDEX),
				mLastFrameTime(0.f),
				mLastFrameIndex(0)
			{
			}

			bool prep(Asset& asset);

			const Sampler& operator=(const lljson& src);
			void serialize(lljson& dst) const;

			// Gets the frame index and time for the specified time. 'asset' is
			// the asset to reference for Accessors, 'time' is the animation
			// time to get the frame info for 'frame_idx' is the index of the
			// closest frame that precedes the specified time, and 't' is the
			// interpolant value between the frameIndex and the next frame.
			void getFrameInfo(Asset& asset, F32 time, U32& frame_idx, F32& t);

		public:
			std::vector<F32>	mFrameTimes;
			std::string			mInterpolation;
			F32					mMinTime;
			F32					mMaxTime;
			S32					mInput;
			S32					mOutput;
			F32					mLastFrameTime;
			U32					mLastFrameIndex;
		};

		class Channel
		{
		protected:
			LOG_CLASS(LLGLTF::Animation::Channel);

		public:
			LL_INLINE Channel()
			:	mSampler(INVALID_INDEX)
			{
			}

			const Channel& operator=(const lljson& src);
			void serialize(lljson& dst) const;

			class Target
			{
			public:
				LL_INLINE Target()
				:	mNode(INVALID_INDEX)
				{
				}

				bool operator==(const Target& other) const;
				bool operator!=(const Target& other) const;

				const Target& operator=(const lljson& src);
				void serialize(lljson& dst) const;

			public:
				std::string	mPath;
				S32			mNode;
			};

		public:
			std::string	mTargetPath;
			std::string	mName;
			Target		mTarget;
			S32			mSampler;
		};

		class alignas(16) RotationChannel final : public Channel
		{
		protected:
			LOG_CLASS(LLGLTF::Animation::RotationChannel);

		public:
			LL_ALIGNED16_NEW_DELETE

			RotationChannel() = default;

			LL_INLINE RotationChannel(const Channel& channel)
			:	Channel(channel)
			{
			}

			// Prepares data needed for rendering. 'asset' is the asset to
			// reference for Accessors. 'sampler' is the sampler associated
			// with this channel.
			bool prep(Asset& asset, Sampler& sampler);

			void apply(Asset& asset, Sampler& sampler, F32 time);

		public:
			alignas(16) std::vector<quat> mRotations;
		};

		class alignas(16) TranslationChannel final : public Channel
		{
		protected:
			LOG_CLASS(LLGLTF::Animation::TranslationChannel);

		public:
			LL_ALIGNED16_NEW_DELETE

			TranslationChannel() = default;

			LL_INLINE TranslationChannel(const Channel& channel)
			:	Channel(channel)
			{
			}

			// Prepares data needed for rendering. 'asset' is the asset to
			// reference for Accessors. 'sampler' is the sampler associated
			// with this channel.
			bool prep(Asset& asset, Sampler& sampler);

			void apply(Asset& asset, Sampler& sampler, F32 time);

		public:
			alignas(16) std::vector<vec3> mTranslations;
		};

		class alignas(16) ScaleChannel final : public Channel
		{
		protected:
			LOG_CLASS(LLGLTF::Animation::ScaleChannel);

		public:
			LL_ALIGNED16_NEW_DELETE

			ScaleChannel() = default;

			LL_INLINE ScaleChannel(const Channel& channel)
			:	Channel(channel)
			{
			}

			// Prepares data needed for rendering. 'asset' is the asset to
			// reference for Accessors. 'sampler' is the sampler associated
			// with this channel.
			bool prep(Asset& asset, Sampler& sampler);

			void apply(Asset& asset, Sampler& sampler, F32 time);

		public:
			alignas(16) std::vector<vec3> mScales;
		};

		const Animation& operator=(const lljson& src);
		void serialize(lljson& dst) const;
		
		bool prep(Asset& asset);

		void update(Asset& asset, float dt);

		// apply this animation at the specified time
		void apply(Asset& asset, F32 time);

	public:
		alignas(16) std::vector<RotationChannel>	mRotationChannels;
		alignas(16) std::vector<TranslationChannel>	mTranslationChannels;
		alignas(16) std::vector<ScaleChannel>		mScaleChannels;
		std::vector<Sampler>						mSamplers;
		std::string									mName;

		// Min/max time values for all samplers combined
		F32											mMinTime;
		F32											mMaxTime;
		
		// Current time of the animation
		F32											mTime;
	};
}
