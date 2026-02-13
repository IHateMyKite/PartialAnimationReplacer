#include "Hooks.h"
#include "Dumper.h"
#include "ReplacerManager.h"
using namespace PAR;

namespace
{
	constexpr float TIME_DELTA = 1.f;
	static float& deltaTime_stub = *(float*)REL::VariantID(523660, 410199, 0x30C3A08).address();
}

void Hooks::Install()
{
	auto& trampoline = SKSE::GetTrampoline();
	auto address = REL::RelocationID(35565, 36564).address();
	auto offset = REL::VariantOffset(0x53, 0x6E, 0x68).offset();
	_OnFrameUpdate = trampoline.write_call<5>(address + offset, OnFrameUpdate);
	logger::info("Hooks installed");
}

void Hooks::OnFrameUpdate(RE::PlayerCharacter* a_this)
{
	if (const auto player = RE::PlayerCharacter::GetSingleton()) {
		ReplacerManager::ApplyReplacers();
	}

	_OnFrameUpdate(a_this);

	_lastUpdated += deltaTime_stub;
	if (!_loaded || _lastUpdated >= TIME_DELTA) {
		_loaded = true;
		_lastUpdated = 0.f;
		std::thread([]() {
			ReplacerManager::EvaluateReplacers();
		}).detach();
	}

	Dumper::OnFrame();
}