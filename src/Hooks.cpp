#include "Hooks.h"
#include "Dumper.h"
#include "ReplacerManager.h"

using namespace PAR;

namespace
{
	constexpr float kEvalInterval = 1.0f;

	static float& g_deltaTime = *reinterpret_cast<float*>(REL::VariantID(523660, 410199, 0x30C3A08).address());

	static inline float g_timeSinceEval = kEvalInterval;

	struct UpdateThirdPerson
	{
		static void thunk(RE::NiAVObject* a_obj, RE::NiUpdateData* a_updateData)
		{
			ReplacerManager::ApplyReplacers();

			g_timeSinceEval += g_deltaTime;

			if (g_timeSinceEval >= kEvalInterval) {
				g_timeSinceEval = 0.0f;
				std::thread([]() {
					ReplacerManager::EvaluateReplacers();
				}).detach();
			}

			func(a_obj, a_updateData);
			Dumper::OnFrame();
		}

		static inline REL::Relocation<decltype(thunk)> func;
		static inline constexpr std::size_t size{ 5 };
	};
}

void Hooks::Install()
{
	if (REL::Module::IsVR()) {
		stl::write_thunk_call<UpdateThirdPerson>(REL::Offset(0x6c6a7d).address());
	} else {
		stl::write_thunk_call<UpdateThirdPerson>(REL::RelocationID(39446, 40522).address() + 0x94);
	}

	logger::info("Hooks installed");
}