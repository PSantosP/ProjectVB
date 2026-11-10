// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/ScopeExit.h"
#include "Unit/VBUITestFixture.h"
#include "Unit/VBVibrationProbeController.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Settings/VBOptionsSubsystem.h"

// ApplyUserSettings 를 부르면 UI 배율(엔진 설정 CDO)까지 바뀌고, 확정 경로(Commit)는 기계 설정까지 디스크에 써서 시험이 부르지 않는다.
//   ApplyVibrationTo 를 직접 부르고, ApplyUserSettings -> ApplyVibration 연결은 아래 새 PC 길이 못박는다.
// 진동 옵션 하나가 PC 의 두 문(일반 진동, 햅틱)을 같이 닫고, 새 PC 에도 걸린다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBOptionVibrationTest, "VB.Unit.Options.Vibration",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBOptionVibrationTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("vibration defaults on (engine default)"), FVBSettingsSaveData().bVibrationEnabled);

	VBUITest::FRowFixture Fixture;
	ON_SCOPE_EXIT { Fixture.Shutdown(); };
	if (!Fixture.Initialize(*this, AVBVibrationProbeController::StaticClass()))
	{
		return false;
	}

	AVBVibrationProbeController* Probe = Cast<AVBVibrationProbeController>(Fixture.Controller);
	UVBOptionsSubsystem* Options = UVBOptionsSubsystem::Get(Fixture.LocalPlayer);
	if (!TestNotNull(TEXT("probe controller"), Probe) || !TestNotNull(TEXT("options subsystem"), Options))
	{
		return false;
	}

	UVBOptionsSubsystem::ApplyVibrationTo(*Probe, /*bEnabled=*/false);
	TestFalse(TEXT("off closes the force feedback gate"), static_cast<bool>(Probe->bForceFeedbackEnabled));
	TestTrue(TEXT("off disables haptics"), Probe->LastDisableHaptics.Get(false));

	// 새 PC 길: 이 월드에는 게임 인스턴스가 없어 GetSettingsData 가 기본값(켬)을 준다. 꺼 둔 PC 가 다시 켜져야 이 길이 진동까지 거는 것이다.
	Probe->LastDisableHaptics.Reset();
	Options->PlayerControllerChanged(Probe);
	TestTrue(TEXT("controller change re-applies the force feedback gate"), static_cast<bool>(Probe->bForceFeedbackEnabled));
	TestTrue(TEXT("controller change re-applies haptics"), Probe->LastDisableHaptics.IsSet() && !Probe->LastDisableHaptics.GetValue());
	return true;
}
#endif
