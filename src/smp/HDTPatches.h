#include "HavokUtil.h"
#include <Psapi.h>
#include <Windows.h>
#include <vector>
#include <xbyak/xbyak.h>

namespace MemoryHacks
{

	uintptr_t ScanModule(const char* moduleName, const char* signature)
	{
		HMODULE hModule = GetModuleHandleA(moduleName);
		if (!hModule)
			return 0;

		MODULEINFO moduleInfo;
		GetModuleInformation(GetCurrentProcess(), hModule, &moduleInfo, sizeof(moduleInfo));

		uintptr_t start = (uintptr_t)moduleInfo.lpBaseOfDll;
		uintptr_t end = start + moduleInfo.SizeOfImage;

		std::vector<int> pattern;
		std::string sig(signature);
		size_t pos = 0;
		while (pos < sig.length()) {
			if (sig[pos] == ' ') {
				pos++;
				continue;
			}
			if (sig[pos] == '?') {
				pattern.push_back(-1);
				pos++;
				if (pos < sig.length() && sig[pos] == '?')
					pos++;
			} else {
				std::string byteString = sig.substr(pos, 2);
				pattern.push_back(std::stol(byteString, nullptr, 16));
				pos += 2;
			}
		}

		for (uintptr_t i = start; i < end - pattern.size(); i++) {
			bool found = true;
			for (size_t j = 0; j < pattern.size(); j++) {
				if (pattern[j] != -1 && *(unsigned char*)(i + j) != pattern[j]) {
					found = false;
					break;
				}
			}
			if (found)
				return i;
		}
		return 0;
	}
}

namespace CreateOrUpdateSystemHook
{

	// x64 __fastcall layout from disassembly:
	//   RCX = this (SkyrimSystemCreator*)
	//   RDX = skeleton (NiNode*)
	//   R8  = model (NiAVObject*)
	//   R9  = file (pair<>* / PhysicsFile*)
	//   Stack[0x28] = renameMap
	//   Stack[0x30] = oldSystem (SkyrimSystem*)
	// MSVC hidden return ptr for Ref<SkyrimSystem>
	typedef void*(__fastcall* FnCreateOrUpdateSystem)(
		void* retPtr, void* thisPtr, void* skeleton, void* model, void* file,
		void* renameMap, void* oldSystem);

	FnCreateOrUpdateSystem Original_CreateOrUpdateSystem = nullptr;

	// Reset all bones to reference pose before hdt picks them up,
	// so physics init doesn't inherit stale transforms n shit
	void* __fastcall My_CreateOrUpdateSystem(
		void* retPtr, void* thisPtr, void* skeleton, void* model, void* file,
		void* renameMap, void* oldSystem)
	{
		if (skeleton) {
			RE::NiNode* skelNode = static_cast<RE::NiNode*>(skeleton);
			if (skelNode) {
				auto ref = skelNode->GetUserData();
				if (auto actor = ref ? ref->As<RE::Actor>() : nullptr) {
					auto havokSkel = BoneReset::GetAnimationSkeleton(actor);
					if (havokSkel) {
						for (int32_t i = 0; i < havokSkel->bones.size(); ++i) {
							RE::BSFixedString boneName(havokSkel->bones[i].name.data());
							auto boneNode = skelNode->GetObjectByName(boneName);
							if (!boneNode)
								continue;

							RE::hkQsTransform refPose;
							if (BoneReset::GetReferencePoseByIndex(havokSkel, i, refPose)) {
								boneNode->local = BoneReset::HkQsTransformToNiTransform(refPose);
							}
						}
						RE::NiUpdateData updateData;
						skelNode->Update(updateData);
					}
				}
			}
		}

		return Original_CreateOrUpdateSystem(
			retPtr, thisPtr, skeleton, model, file, renameMap, oldSystem);
	}

	void* AllocateCodeNearModule(void* moduleBase, size_t size)
	{
		uintptr_t baseAddr = (uintptr_t)moduleBase;

		SYSTEM_INFO sysInfo;
		GetSystemInfo(&sysInfo);
		uintptr_t granularity = sysInfo.dwAllocationGranularity;

		uintptr_t maxDist = 0x7FFFFFFF;
		uintptr_t minAddr = baseAddr > maxDist ? baseAddr - maxDist : granularity;
		uintptr_t maxAddr = baseAddr + maxDist;

		for (uintptr_t addr = baseAddr; addr < maxAddr; addr += granularity) {
			void* mem = VirtualAlloc((void*)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (mem)
				return mem;
		}
		for (uintptr_t addr = baseAddr; addr > minAddr; addr -= granularity) {
			void* mem = VirtualAlloc((void*)addr, size, MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE);
			if (mem)
				return mem;
		}
		return nullptr;
	}

	void Install()
	{
		SKSE::log::info("[Faster-SMP] Installing call-site hooks...");

		void* hdtModule = GetModuleHandleA("hdtSMP64.dll");
		if (!hdtModule) {
			SKSE::log::error("[Faster-SMP] hdtSMP64.dll not loaded");
			return;
		}

		MODULEINFO modInfo{};
		GetModuleInformation(GetCurrentProcess(), (HMODULE)hdtModule, &modInfo, sizeof(modInfo));
		uintptr_t modBase = (uintptr_t)hdtModule;
		uintptr_t modEnd = modBase + modInfo.SizeOfImage;

		// createOrUpdateSystem prologue - unique due to the 0xB70 stack frame
		// Todo: Verify it works on different versions. Doesn't need to be this specific either since it's so unique
		const char* sigCreateOrUpdate =
			"48 89 5C 24 18 "
			"48 89 74 24 20 "
			"48 89 54 24 10 "
			"48 89 4C 24 08 "
			"57 "
			"41 54 "
			"41 55 "
			"41 56 "
			"41 57 "
			"48 81 EC 70 0B";

		uintptr_t targetFunc = MemoryHacks::ScanModule("hdtSMP64.dll", sigCreateOrUpdate);
		if (!targetFunc) {
			SKSE::log::error("[CreateOrUpdate] Failed to find createOrUpdateSystem");
			return;
		}

		Original_CreateOrUpdateSystem = (FnCreateOrUpdateSystem)targetFunc;
		SKSE::log::info("[CreateOrUpdate] Found createOrUpdateSystem at {:X}", targetFunc);

		// Trampoline island so we can reach our hook from rel32 call sites
		void* islandMem = AllocateCodeNearModule(hdtModule, 64);
		if (!islandMem) {
			SKSE::log::error("[CreateOrUpdate] Failed to allocate trampoline memory");
			return;
		}

		struct IslandGenerator : Xbyak::CodeGenerator
		{
			IslandGenerator(uintptr_t wrapperAddr)
			{
				mov(r10, wrapperAddr);
				jmp(r10);
			}
		};

		IslandGenerator islandCode((uintptr_t)My_CreateOrUpdateSystem);
		std::memcpy(islandMem, islandCode.getCode(), islandCode.getSize());
		FlushInstructionCache(GetCurrentProcess(), islandMem, islandCode.getSize());

		// Find all E8 (near call) sites that target createOrUpdateSystem (Todo: Verify this is fine on non-avx..)
		std::vector<uintptr_t> callSites;
		for (uintptr_t addr = modBase; addr < modEnd - 5; ++addr) {
			if (*(uint8_t*)addr != 0xE8)
				continue;

			int32_t rel = *(int32_t*)(addr + 1);
			uintptr_t callTarget = addr + 5 + (int64_t)rel;

			if (callTarget != targetFunc)
				continue;
			if (callTarget < modBase || callTarget >= modEnd)
				continue;

			callSites.push_back(addr);
		}

		// Just a note to those who come next. This is just the easiest way to hook... Between versions
		// Have very different assembly, and this has the potential to maybe work on FLEX even. 
		SKSE::log::info("[CreateOrUpdate] Found {} call site(s)", callSites.size());

		// Patch each call to go through our island instead 
		int hookedCount = 0;
		for (uintptr_t callAddr : callSites) {
			int64_t newOffset64 = (int64_t)islandMem - (int64_t)callAddr - 5;
			if (newOffset64 > INT32_MAX || newOffset64 < INT32_MIN) {
				SKSE::log::error("[CreateOrUpdate] Island too far from call site at {:X}", callAddr);
				continue;
			}

			DWORD oldProtect;
			VirtualProtect((void*)callAddr, 5, PAGE_EXECUTE_READWRITE, &oldProtect);
			*(int32_t*)(callAddr + 1) = (int32_t)newOffset64;
			VirtualProtect((void*)callAddr, 5, oldProtect, &oldProtect);
			FlushInstructionCache(GetCurrentProcess(), (void*)callAddr, 5);

			SKSE::log::info("[CreateOrUpdate]   Hooked call at {:X} (+{:X})",
				callAddr, callAddr - modBase);
			hookedCount++;
		}

		if (hookedCount == 0)
			SKSE::log::error("[CreateOrUpdate] No call sites were patched!");
		else
			SKSE::log::info("[CreateOrUpdate] Hooked {} call site(s)", hookedCount);

		SKSE::log::info("[Faster-SMP] Hooks finished. Hopefully your game isn't fucked.");
	}

}  // namespace CreateOrUpdateSystemHook