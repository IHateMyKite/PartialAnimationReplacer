#pragma once
#include "RE/Skyrim.h"

namespace RE
{

	class hkaBone
	{
	public:
		hkStringPtr name;
		bool lockTranslation;
	};
	static_assert(sizeof(hkaBone) == 0x10);

	struct LocalFrameOnBone;

	class hkaSkeleton : public hkReferencedObject
	{
	public:
		hkStringPtr name;                       // 10
		hkArray<int16_t> parentIndices;         // 18
		hkArray<hkaBone> bones;                 // 28
		hkArray<hkQsTransform> referencePose;   // 38
		hkArray<float> referenceFloats;         // 48
		hkArray<hkStringPtr> floatSlots;        // 58
		hkArray<LocalFrameOnBone> localFrames;  // 68
	};

}  // namespace RE

namespace BoneReset
{

	inline const RE::hkaSkeleton* GetAnimationSkeleton(RE::Actor* a_actor)
	{
		if (!a_actor)
			return nullptr;

		RE::BSAnimationGraphManagerPtr graphManager;
		if (!a_actor->GetAnimationGraphManager(graphManager))
			return nullptr;
		if (!graphManager || graphManager->graphs.empty())
			return nullptr;

		auto& graph = graphManager->graphs[0];
		if (!graph)
			return nullptr;

		auto& setup = graph->characterInstance.setup;
		if (!setup)
			return nullptr;

		return reinterpret_cast<const RE::hkaSkeleton*>(setup->animationSkeleton.get());
	}

	inline bool GetReferencePoseByIndex(const RE::hkaSkeleton* a_skeleton, int32_t a_boneIndex, RE::hkQsTransform& a_outTransform)
	{
		if (!a_skeleton)
			return false;
		if (a_boneIndex < 0 || a_boneIndex >= a_skeleton->referencePose.size())
			return false;

		a_outTransform = a_skeleton->referencePose[a_boneIndex];
		return true;
	}

	// hkQsTransform > NiTransform (local space)
	inline RE::NiTransform HkQsTransformToNiTransform(const RE::hkQsTransform& a_hkTransform)
	{
		RE::NiTransform result;

		const auto& q = a_hkTransform.rotation;
		float qx = q.vec.quad.m128_f32[0];
		float qy = q.vec.quad.m128_f32[1];
		float qz = q.vec.quad.m128_f32[2];
		float qw = q.vec.quad.m128_f32[3];

		float sqw = qw * qw;
		float sqx = qx * qx;
		float sqy = qy * qy;
		float sqz = qz * qz;

		float invs = 1.0f / (sqx + sqy + sqz + sqw);
		result.rotate.entry[0][0] = (sqx - sqy - sqz + sqw) * invs;
		result.rotate.entry[1][1] = (-sqx + sqy - sqz + sqw) * invs;
		result.rotate.entry[2][2] = (-sqx - sqy + sqz + sqw) * invs;

		float tmp1 = qx * qy;
		float tmp2 = qz * qw;
		result.rotate.entry[1][0] = 2.0f * (tmp1 + tmp2) * invs;
		result.rotate.entry[0][1] = 2.0f * (tmp1 - tmp2) * invs;

		tmp1 = qx * qz;
		tmp2 = qy * qw;
		result.rotate.entry[2][0] = 2.0f * (tmp1 - tmp2) * invs;
		result.rotate.entry[0][2] = 2.0f * (tmp1 + tmp2) * invs;

		tmp1 = qy * qz;
		tmp2 = qx * qw;
		result.rotate.entry[2][1] = 2.0f * (tmp1 + tmp2) * invs;
		result.rotate.entry[1][2] = 2.0f * (tmp1 - tmp2) * invs;

		result.translate.x = a_hkTransform.translation.quad.m128_f32[0];
		result.translate.y = a_hkTransform.translation.quad.m128_f32[1];
		result.translate.z = a_hkTransform.translation.quad.m128_f32[2];

		result.scale = a_hkTransform.scale.quad.m128_f32[0];

		return result;
	}

}  // namespace BoneReset