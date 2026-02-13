#pragma once
namespace PAR
{
	class Hooks
	{
	public:
		static void Install();

	private:
		static void OnFrameUpdate(RE::PlayerCharacter* a_this);
		static inline REL::Relocation<decltype(OnFrameUpdate)> _OnFrameUpdate;

		static inline float _lastUpdated = 0.f;
		static inline bool _loaded = false;
	};
}