// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Settings/VBGameUserSettings.h"
#include "Settings/VBOptionRules.h"
#include "Settings/VBOptionsSettings.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>

// 프레임 예산 - 설정 객체는 시험용 임시 객체라 읽기, 저장을 부르지 않고, 건 값은 엔진 cvar 대신 주입 함수로 받는다.

class FVBFrameBudgetTestAccess
{
public:
	static void Capture(UVBGameUserSettings& Settings, TArray<float>& Applied)
	{
		Settings.ApplyLimitForTests = [&Applied](float Limit) { Applied.Add(Limit); };
	}

	static void SetRuntimeCapsAllowed(UVBGameUserSettings& Settings, bool bAllowed) { Settings.AllowsRuntimeCapsForTests = bAllowed; }
	static void SetAppActive(UVBGameUserSettings& Settings, bool bActive) { Settings.bAppActive = bActive; Settings.ReapplyFrameRateLimit(); }
	static void SetOnBattery(UVBGameUserSettings& Settings, bool bBattery) { Settings.bOnBattery = bBattery; Settings.ReapplyFrameRateLimit(); }
	static void Reapply(UVBGameUserSettings& Settings) { Settings.ReapplyFrameRateLimit(); }
	static void Start(UVBGameUserSettings& Settings) { Settings.StartRuntimePolicy(); }
	static bool Started(const UVBGameUserSettings& Settings) { return Settings.bRuntimePolicyStarted; }
	static bool HasTicker(const UVBGameUserSettings& Settings) { return Settings.PolicyTicker.IsValid(); }
};

namespace VBFrameBudgetTests
{
	using FrameAccess = FVBFrameBudgetTestAccess;
	constexpr EAutomationTestFlags FrameFlags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	// 시험용 설정 객체. 사용자 제한만 정하고 엔진 쪽으로는 아무것도 쓰지 않는다.
	TStrongObjectPtr<UVBGameUserSettings> MakeSettings(float UserLimit, TArray<float>& Applied)
	{
		TStrongObjectPtr<UVBGameUserSettings> Settings(NewObject<UVBGameUserSettings>(GetTransientPackage(), NAME_None, RF_Transient));
		FrameAccess::Capture(*Settings, Applied);
		FrameAccess::SetRuntimeCapsAllowed(*Settings, true);
		Settings->SetFrameRateLimit(UserLimit);
		return Settings;
	}
}

// 합치기 표: 양수 중 가장 작은 값, 하나도 없으면 0. 0, 음수, NaN, 무한은 상한이 아니다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetCombineTest, "VB.Unit.FrameBudget.Combine", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetCombineTest::RunTest(const FString& Parameters)
{
	using VBOptionRules::CombineFrameRateLimits;
	TestEqual(TEXT("no caps -> user"), CombineFrameRateLimits(144.0f, {}), 144.0f);
	TestEqual(TEXT("unlimited user, no caps -> unlimited"), CombineFrameRateLimits(0.0f, {}), 0.0f);
	TestEqual(TEXT("unlimited user takes the cap"), CombineFrameRateLimits(0.0f, { 30.0f }), 30.0f);
	TestEqual(TEXT("lowest of user and caps"), CombineFrameRateLimits(144.0f, { 60.0f, 30.0f, 60.0f }), 30.0f);
	TestEqual(TEXT("lower user survives every cap"), CombineFrameRateLimits(20.0f, { 60.0f, 30.0f }), 20.0f);
	TestEqual(TEXT("cap 0 is no cap"), CombineFrameRateLimits(144.0f, { 0.0f }), 144.0f);
	TestEqual(TEXT("negative cap is no cap"), CombineFrameRateLimits(144.0f, { -5.0f }), 144.0f);
	TestEqual(TEXT("NaN cap is no cap"), CombineFrameRateLimits(144.0f, { std::numeric_limits<float>::quiet_NaN() }), 144.0f);
	TestEqual(TEXT("infinite cap is no cap"), CombineFrameRateLimits(144.0f, { std::numeric_limits<float>::infinity() }), 144.0f);
	TestEqual(TEXT("NaN user with a cap -> cap"), CombineFrameRateLimits(std::numeric_limits<float>::quiet_NaN(), { 60.0f }), 60.0f);
	return true;
}

// 배터리 판정 표: 전원이 끊기고 배터리가 있을 때만. 충전 중, 연결, 배터리 없음, 모름, 조회 실패는 아니다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetBatteryTest, "VB.Unit.FrameBudget.Battery", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetBatteryTest::RunTest(const FString& Parameters)
{
	using VBOptionRules::IsOnBatteryPower;
	constexpr uint8 AC_Offline = 0, AC_Online = 1, AC_Unknown = 255;
	constexpr uint8 High = 1, Charging = 8, HighCharging = 9, NoBattery = 128, Unknown = 255;
	TestTrue(TEXT("unplugged with a battery"), IsOnBatteryPower(true, AC_Offline, High));
	TestFalse(TEXT("plugged in and charging"), IsOnBatteryPower(true, AC_Online, HighCharging));
	TestFalse(TEXT("plugged in, full"), IsOnBatteryPower(true, AC_Online, High));
	TestFalse(TEXT("charging flag while AC unknown"), IsOnBatteryPower(true, AC_Unknown, Charging));
	TestFalse(TEXT("desktop without battery"), IsOnBatteryPower(true, AC_Offline, NoBattery));
	TestFalse(TEXT("battery state unknown"), IsOnBatteryPower(true, AC_Offline, Unknown));
	TestFalse(TEXT("query failed"), IsOnBatteryPower(false, AC_Offline, High));
	return true;
}

// 요청은 손잡이 하나씩: 둘 중 하나를 거둬도 메뉴 제한이 남고, 둘 다 거두면 풀린다. 같은 원함 반복은 요청을 늘리지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetRequestsTest, "VB.Unit.FrameBudget.Requests", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetRequestsTest::RunTest(const FString& Parameters)
{
	TArray<float> Applied;
	TStrongObjectPtr<UVBGameUserSettings> Settings = VBFrameBudgetTests::MakeSettings(144.0f, Applied);
	const float Menu = GetDefault<UVBOptionsSettings>()->MenuFrameRateLimit;
	if (!TestTrue(TEXT("menu limit below user limit for this table"), Menu > 0.0f && Menu < 144.0f))
	{
		return false;
	}

	FVBFrameRateRequestHandle Pause;
	FVBFrameRateRequestHandle MainMenu = Settings->AddMenuFrameRateRequest(TEXT("MainMenu"));
	TestEqual(TEXT("one request -> menu limit"), Settings->GetEffectiveFrameRateLimit(), Menu);
	// 정지 화면 목록은 같은 원함으로 여러 번 부른다(화면 A 켜짐, B 켜짐, A 꺼짐).
	Settings->SyncMenuFrameRateRequest(Pause, true, TEXT("PauseScreen"));
	Settings->SyncMenuFrameRateRequest(Pause, true, TEXT("PauseScreen"));
	Settings->SyncMenuFrameRateRequest(Pause, true, TEXT("PauseScreen"));
	Settings->RemoveMenuFrameRateRequest(MainMenu);
	TestFalse(TEXT("removed handle is cleared"), MainMenu.IsValid());
	TestEqual(TEXT("the other requester still holds the menu limit"), Settings->GetEffectiveFrameRateLimit(), Menu);
	Settings->SyncMenuFrameRateRequest(Pause, false, TEXT("PauseScreen"));
	TestFalse(TEXT("repeated wants were one request"), Settings->HasMenuFrameRateRequest());
	TestEqual(TEXT("all released -> user limit"), Settings->GetEffectiveFrameRateLimit(), 144.0f);

	FVBFrameRateRequestHandle Stale;
	Stale.Id = 12345;
	Settings->RemoveMenuFrameRateRequest(Stale);
	Settings->RemoveMenuFrameRateRequest(MainMenu);
	TestFalse(TEXT("unknown and repeated removals do nothing"), Settings->HasMenuFrameRateRequest());
	if (TestEqual(TEXT("applied twice: menu once, user once"), Applied.Num(), 2))
	{
		TestEqual(TEXT("first write = menu"), Applied[0], Menu);
		TestEqual(TEXT("second write = user"), Applied[1], 144.0f);
	}

	return true;
}

// 창이 뒤, 배터리 상한은 허용된 실행에서만 걸리고, 사용자가 더 낮게 골랐으면 그 값이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetRuntimeCapsTest, "VB.Unit.FrameBudget.RuntimeCaps", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetRuntimeCapsTest::RunTest(const FString& Parameters)
{
	TArray<float> Applied;
	TStrongObjectPtr<UVBGameUserSettings> Settings = VBFrameBudgetTests::MakeSettings(144.0f, Applied);
	const UVBOptionsSettings* Options = GetDefault<UVBOptionsSettings>();
	const float Background = Options->FrameRateLimitWhenBackgrounded;
	const float Battery = Options->FrameRateLimitOnBattery;
	if (!TestTrue(TEXT("defaults are positive and below the user limit"), Background > 0.0f && Battery > 0.0f
		&& Background < 144.0f && Battery < 144.0f))
	{
		return false;
	}

	FVBFrameBudgetTestAccess::SetAppActive(*Settings, false);
	TestEqual(TEXT("backgrounded"), Settings->GetEffectiveFrameRateLimit(), Background);
	FVBFrameBudgetTestAccess::SetOnBattery(*Settings, true);
	TestEqual(TEXT("backgrounded on battery -> lower of the two"), Settings->GetEffectiveFrameRateLimit(), FMath::Min(Background, Battery));
	FVBFrameBudgetTestAccess::SetAppActive(*Settings, true);
	TestEqual(TEXT("active on battery"), Settings->GetEffectiveFrameRateLimit(), Battery);

	FVBFrameBudgetTestAccess::SetRuntimeCapsAllowed(*Settings, false);
	TestEqual(TEXT("host / PIE: new caps off"), Settings->GetEffectiveFrameRateLimit(), 144.0f);
	FVBFrameRateRequestHandle Menu = Settings->AddMenuFrameRateRequest(TEXT("PauseScreen"));
	TestEqual(TEXT("menu cap still applies where new caps are off"), Settings->GetEffectiveFrameRateLimit(), Options->MenuFrameRateLimit);
	Settings->RemoveMenuFrameRateRequest(Menu);

	FVBFrameBudgetTestAccess::SetRuntimeCapsAllowed(*Settings, true);
	Settings->SetFrameRateLimit(20.0f);
	TestEqual(TEXT("a lower user limit wins on battery"), Settings->GetEffectiveFrameRateLimit(), 20.0f);
	return true;
}

// 결과 값이 같으면 다시 걸지 않는다 - 상태 이름이 아니라 값으로 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetApplyOnChangeTest, "VB.Unit.FrameBudget.ApplyOnlyOnChange", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetApplyOnChangeTest::RunTest(const FString& Parameters)
{
	TArray<float> Applied;
	TStrongObjectPtr<UVBGameUserSettings> Settings = VBFrameBudgetTests::MakeSettings(20.0f, Applied);
	FVBFrameBudgetTestAccess::Reapply(*Settings);
	const int32 AfterFirst = Applied.Num();
	TestEqual(TEXT("first apply writes once"), AfterFirst, 1);
	FVBFrameBudgetTestAccess::SetAppActive(*Settings, false);
	FVBFrameBudgetTestAccess::SetOnBattery(*Settings, true);
	FVBFrameRateRequestHandle Menu = Settings->AddMenuFrameRateRequest(TEXT("PauseScreen"));
	TestEqual(TEXT("user 20 under every cap -> no new write"), Applied.Num(), AfterFirst);
	Settings->RemoveMenuFrameRateRequest(Menu);
	FVBFrameBudgetTestAccess::Reapply(*Settings);
	TestEqual(TEXT("still no new write"), Applied.Num(), AfterFirst);
	return true;
}

// 임시 설정 객체(옵션의 기본값 초안 같은 것)는 창 활성, 전원 감시를 시작하지 않는다 - 엔진 설정 객체 하나만.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFrameBudgetTempObjectTest, "VB.Unit.FrameBudget.TempObjectDoesNotStart", VBFrameBudgetTests::FrameFlags)
bool FVBFrameBudgetTempObjectTest::RunTest(const FString& Parameters)
{
	TArray<float> Applied;
	TStrongObjectPtr<UVBGameUserSettings> Settings = VBFrameBudgetTests::MakeSettings(144.0f, Applied);
	FVBFrameBudgetTestAccess::Start(*Settings);
	TestFalse(TEXT("temp object not started"), FVBFrameBudgetTestAccess::Started(*Settings));
	TestFalse(TEXT("temp object has no ticker"), FVBFrameBudgetTestAccess::HasTicker(*Settings));
	TestEqual(TEXT("temp object wrote nothing"), Applied.Num(), 0);
	return true;
}

#endif
