// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include <limits>
#include "InputModifiers.h"
#include "Input/VBInputModifiers.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameTypes.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "Serialization/ObjectAndNameAsStringProxyArchive.h"
#include "Settings/VBOptionCatalog.h"
#include "Settings/VBOptionRules.h"
#include "Settings/VBOptionsDraft.h"
#include "Settings/VBOptionsSettings.h"

// 계산은 월드 없이, 옛 저장은 직렬화 건너뛰기로 흉내 낸다.

// 데드존: 엔진 DeadZone(원형) 과 같은 값을 내는가 - 기본값으로 바꿔 끼워도 손맛이 그대로라는 근거(4절 4행).
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputCalibrationDeadZoneTest, "VB.Unit.Input.Calibration.DeadZone",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInputCalibrationDeadZoneTest::RunTest(const FString& Parameters)
{
	UInputModifierDeadZone* Engine = NewObject<UInputModifierDeadZone>();
	Engine->Type = EDeadZoneType::Radial;
	Engine->UpperThreshold = 1.0f;
	const TArray<FVector2D> Samples = {
		{0.0, 0.0}, {0.1, 0.0}, {0.15, 0.0}, {0.2, 0.0}, {0.5, 0.0}, {0.6, 0.8}, {-0.3, 0.4}, {0.9, -0.9}, {2.0, 0.0}, {0.01, 0.149} };
	for (const float Lower : { 0.15f, 0.2f, 0.5f })
	{
		Engine->LowerThreshold = Lower;
		for (const FVector2D& Sample : Samples)
		{
			const FInputActionValue In(Sample);
			const FVector Expected = Engine->ModifyRaw(nullptr, In, 0.0f).Get<FVector>();
			const FVector Ours = VBInputCalibration::ApplyRadialDeadZone(In.Get<FVector>(), EInputActionValueType::Axis2D, Lower);
			TestTrue(*FString::Printf(TEXT("lower %.2f sample (%.3f, %.3f) matches the engine dead zone"), Lower, Sample.X, Sample.Y),
				Ours.Equals(Expected, 1e-5));
		}
	}

	TestTrue(TEXT("inside the dead zone is zero"),
		VBInputCalibration::ApplyRadialDeadZone(FVector(0.1, 0.0, 0.0), EInputActionValueType::Axis2D, 0.15f).IsZero());
	// 엔진은 아래 = 위 문턱에서 0 으로 나눈다. 우리는 잘라서 유한한 값을 낸다.
	for (const float Bad : { 1.0f, 1.5f, -0.2f, std::numeric_limits<float>::quiet_NaN() })
	{
		const FVector Out = VBInputCalibration::ApplyRadialDeadZone(FVector(0.95, 0.0, 0.0), EInputActionValueType::Axis2D, Bad);
		TestFalse(*FString::Printf(TEXT("threshold %f gives a finite value"), Bad), Out.ContainsNaN());
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputCalibrationLookTest, "VB.Unit.Input.Calibration.Look",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInputCalibrationLookTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("sensitivity multiplies both axes"), VBInputCalibration::ApplyLook({1.0, 2.0}, 1.6f, false).Equals({1.6, 3.2}, 1e-5));
	TestTrue(TEXT("invert flips Y only"), VBInputCalibration::ApplyLook({1.0, 2.0}, 1.6f, true).Equals({1.6, -3.2}, 1e-5));

	FVBSettingsSaveData Settings;
	Settings.LookSensitivity = 1.4f;
	Settings.GamepadLookSensitivity = 2.2f;
	TestEqual(TEXT("mouse uses the mouse value"), VBInputCalibration::LookSensitivity(Settings, EVBLookDevice::Mouse), 1.4f);
	TestEqual(TEXT("touch uses the pointer (mouse) value"), VBInputCalibration::LookSensitivity(Settings, EVBLookDevice::Touch), 1.4f);
	TestEqual(TEXT("gamepad uses its own value once chosen"), VBInputCalibration::LookSensitivity(Settings, EVBLookDevice::Gamepad), 2.2f);

	// 로컬 플레이어가 없으면 구조체 기본값(감도 1, 반전 끔)이라 값이 그대로다 - 원격, 빙의 전 입력을 바꾸지 않는다.
	UVBInputModifier_LookCalibration* Modifier = NewObject<UVBInputModifier_LookCalibration>();
	const FVector Out = Modifier->ModifyRaw(nullptr, FInputActionValue(FVector2D(0.7, -0.3)), 0.0f).Get<FVector>();
	TestTrue(TEXT("no local player leaves the value unchanged"), Out.Equals(FVector(0.7, -0.3, 0.0), 1e-6));
	return true;
}

// 락온의 "손으로 돌렸다" 알림은 두 축이 IsNearlyZero 인지만 본다. 감도가 판정을 바꾸는 구간(1e-8 ~ 1e-7)에
//   장치가 내는 가장 작은 0 아닌 값이 닿지 않는다는 것을 못박는다. 가장 작은 값 = 16비트 스틱 한 눈금이 데드존을 막 넘은 값.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInputCalibrationNearZeroTest, "VB.Unit.Input.Calibration.NearZero",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInputCalibrationNearZeroTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = GetDefault<UVBOptionsSettings>();
	const float Step = 1.0f / 32768.0f;
	const float Lower = FVBSettingsSaveData().GamepadLookDeadZone;
	const FVector Smallest = VBInputCalibration::ApplyRadialDeadZone(FVector(Lower + Step, 0.0, 0.0), EInputActionValueType::Axis2D, Lower);
	TestFalse(TEXT("smallest stick value past the dead zone is not nearly zero"), FMath::IsNearlyZero(Smallest.X));
	const FVector2D Scaled = VBInputCalibration::ApplyLook(FVector2D(Smallest.X, 0.0), Limits->LookSensitivityRange.Min, false);
	TestFalse(TEXT("x lowest sensitivity is still not nearly zero"), FMath::IsNearlyZero(Scaled.X));
	return true;
}

// 2판 P2-1: 패드 감도를 고르기 전에는 마우스 감도를 따르고, 고르면 그때부터 따로 간다. 옛 저장(칸 없음)도 같다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGamepadSensitivitySaveTest, "VB.Unit.Options.GamepadSensitivitySave",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGamepadSensitivitySaveTest::RunTest(const FString& Parameters)
{
	const UVBOptionsSettings* Limits = GetDefault<UVBOptionsSettings>();

	// 옛 저장: 새 세 칸을 빼고 쓴 뒤 읽는다(BloodDisplaySave 와 같은 흉내).
	struct FLegacyWriter : FObjectAndNameAsStringProxyArchive
	{
		explicit FLegacyWriter(FArchive& Inner) : FObjectAndNameAsStringProxyArchive(Inner, false) { ArNoDelta = true; }
		mutable int32 Omitted = 0;
		virtual bool ShouldSkipProperty(const FProperty* Property) const override
		{
			if (Property->GetOwnerStruct() == FVBSettingsSaveData::StaticStruct())
			{
				for (const TCHAR* NewField : { TEXT("GamepadLookSensitivity"), TEXT("GamepadLookDeadZone"), TEXT("GamepadMoveDeadZone"), TEXT("bVibrationEnabled") })
				{
					if (Property->GetFName() == NewField)
					{
						++Omitted;
						return true;
					}
				}
			}

			return FObjectAndNameAsStringProxyArchive::ShouldSkipProperty(Property);
		}
	};
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Save->Settings.LookSensitivity = 1.7f;
	Save->Settings.GamepadLookSensitivity = 2.5f; // 빠져야 하는 값 - 읽은 쪽에 남으면 흉내가 틀린 것
	Save->Settings.bVibrationEnabled = false; // L3b 칸도 같이 뺀다(검증 WARN: 진동 칸의 옛 저장 시험)
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes, true);
	FLegacyWriter Legacy(Writer);
	Save->Serialize(Legacy);
	TestEqual(TEXT("legacy payload skips the four new fields"), Legacy.Omitted, 4);
	UVBSaveGame* Loaded = NewObject<UVBSaveGame>();
	FMemoryReader Reader(Bytes, true);
	FObjectAndNameAsStringProxyArchive LegacyReader(Reader, true);
	Loaded->Serialize(LegacyReader);
	TestEqual(TEXT("legacy save reads pad sensitivity as not chosen"), Loaded->Settings.GamepadLookSensitivity, -1.0f);
	TestEqual(TEXT("legacy pad look follows the mouse value"), VBInputCalibration::GamepadLookSensitivity(Loaded->Settings), 1.7f);
	TestEqual(TEXT("legacy look dead zone = the asset's old value"), Loaded->Settings.GamepadLookDeadZone, 0.15f);
	TestEqual(TEXT("legacy move dead zone = the asset's old value"), Loaded->Settings.GamepadMoveDeadZone, 0.2f);
	TestTrue(TEXT("legacy save keeps vibration on"), Loaded->Settings.bVibrationEnabled);

	// 따라가기: 마우스를 바꾸면 패드도 바뀐다. Sanitize 는 -1 을 지운다면 이 줄이 깨진다.
	FVBSettingsSaveData Settings = VBOptionRules::Sanitize(Loaded->Settings, *Limits);
	TestEqual(TEXT("sanitize keeps the not-chosen marker"), Settings.GamepadLookSensitivity, -1.0f);
	Settings.LookSensitivity = 0.6f;
	TestEqual(TEXT("pad follows a later mouse change"), VBInputCalibration::GamepadLookSensitivity(Settings), 0.6f);

	// 옵션 줄: 보이는 값은 풀이값, 움직이면 실제 값이 들어가고 그때부터 독립.
	FVBOptionCatalogContext Context;
	Context.Settings = Limits;
	const TArray<FVBOptionEntry> Controls = VBOptionCatalog::BuildTab(EVBOptionTab::Controls, Context);
	const FVBOptionEntry* Row = Controls.FindByPredicate([](const FVBOptionEntry& E) { return E.Id == TEXT("GamepadLookSensitivity"); });
	if (!TestNotNull(TEXT("pad sensitivity row"), Row))
	{
		return false;
	}

	FVBOptionsDraft Draft;
	Draft.User = Settings;
	TestEqual(TEXT("row shows the followed value"), Row->Get(Draft), 0.6f);
	Row->Set(Draft, 1.9f);
	TestEqual(TEXT("moving the row stores a real value"), Draft.User.GamepadLookSensitivity, 1.9f);
	Draft.User.LookSensitivity = 0.3f;
	TestEqual(TEXT("after choosing, pad no longer follows the mouse"), VBInputCalibration::GamepadLookSensitivity(Draft.User), 1.9f);
	// 마우스 값과 같은 값을 고르면 다시 "따른다" - 줄의 Set(Get) 이 초안을 바꾸지 않고(Options.Catalog), "기본값" 단추도 따르기로 돌아간다.
	Row->Set(Draft, 0.3f);
	TestEqual(TEXT("choosing the mouse value returns to following"), Draft.User.GamepadLookSensitivity, -1.0f);
	const FVBOptionsDraft Before = Draft;
	Row->Set(Draft, Row->Get(Draft));
	TestTrue(TEXT("set(get) keeps a following draft unchanged"), Draft == Before);

	// 범위: 감도는 시점 감도 범위, 데드존은 데드존 범위로 자른다.
	FVBSettingsSaveData Wild;
	Wild.GamepadLookSensitivity = 99.0f;
	Wild.GamepadLookDeadZone = 3.0f;
	Wild.GamepadMoveDeadZone = -1.0f;
	const FVBSettingsSaveData Clean = VBOptionRules::Sanitize(Wild, *Limits);
	TestEqual(TEXT("pad sensitivity clamps to the look range"), Clean.GamepadLookSensitivity, Limits->LookSensitivityRange.Max);
	TestEqual(TEXT("look dead zone clamps to its range"), Clean.GamepadLookDeadZone, Limits->StickDeadZoneRange.Max);
	TestEqual(TEXT("move dead zone clamps to its range"), Clean.GamepadMoveDeadZone, Limits->StickDeadZoneRange.Min);
	return true;
}
#endif
