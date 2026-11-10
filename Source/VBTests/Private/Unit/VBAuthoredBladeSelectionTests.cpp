#include "Misc/AutomationTest.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBAirComboConfig.h"
#include "Data/VBComboSelectionPolicy.h"

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAuthoredBladeSelectionTest, "VB.Unit.Combat.AuthoredBladeSelection",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBAuthoredBladeSelectionTest::RunTest(const FString& Parameters)
{
	UVBAttackConfig* Config = LoadObject<UVBAttackConfig>(nullptr, TEXT("/Game/VB/Data/DA_Attack_Blade_Light"));
	if (!TestNotNull(TEXT("authored Blade attack"), Config)
		|| !TestNotNull(TEXT("mixed attack roles require contextual selection"), Config->ComboSelection.Get()))
	{
		return false;
	}
	TestEqual(TEXT("attack owns selection policy"), Config->ComboSelection->GetOuter(), static_cast<UObject*>(Config));
	const TArray<FVBComboCandidate> Candidates = Config->GetComboCandidates();
	FVBComboStartMemory Memory;
	const auto CheckRole = [&](FVBComboSelectContext Context, EVBComboRole Expected, const TCHAR* Label)
	{
		for (int32 Start = 0; Start < 12; ++Start)
		{
			const FVBComboChoice Choice = Config->ComboSelection->Select(Context, Candidates, Memory, Config);
			if (!TestTrue(TEXT("selected candidate exists"), Candidates.IsValidIndex(Choice.Variant))) { return; }
			const EVBComboRole Role = Candidates[Choice.Variant].Role;
			TestTrue(Label, Role == Expected);
			Memory.NoteStart(Config, Choice.Variant, Role);
		}
	};
	CheckRole({false, 0.0f, false}, EVBComboRole::Neutral, TEXT("stationary no-target input never cycles into sprint"));
	CheckRole({true, 130.0f, false}, EVBComboRole::Close, TEXT("near target uses close attacks"));
	CheckRole({true, 300.0f, false}, EVBComboRole::Neutral, TEXT("far stationary input never selects run attack"));
	CheckRole({true, 300.0f, true}, EVBComboRole::Sprint, TEXT("qualified sprint input selects run attack"));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAuthoredAirHeightTest, "VB.Unit.Combat.AuthoredAirHeight",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FVBAuthoredAirHeightTest::RunTest(const FString& Parameters)
{
	for (const TCHAR* Path : {TEXT("/Game/VB/Data/DA_AirCombo_Blade"), TEXT("/Game/VB/Data/DA_AirCombo_Fighter")})
	{
		const UVBAirComboConfig* Config = LoadObject<UVBAirComboConfig>(nullptr, Path);
		if (!TestNotNull(Path, Config)) { continue; }
		TestEqual(TEXT("air pursuit reaches the victim height"), Config->ChaseHeightOffset, 0.0f);
		TestTrue(TEXT("pair keeps an authored horizontal attack distance"), Config->ChaseStopDistance > 0.0f);
	}
	return true;
}
#endif
