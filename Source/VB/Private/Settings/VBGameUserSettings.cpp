// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Settings/VBGameUserSettings.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/IConsoleManager.h"
#include "HAL/PlatformMisc.h"
#include "Misc/ConfigCacheIni.h"
#include "Settings/VBOptionRules.h"
#include "Settings/VBOptionsSettings.h"
#include "VB/VB.h"

#if PLATFORM_WINDOWS
#include "Windows/WindowsHWrapper.h"   // GetSystemPowerStatus
#endif

namespace
{
	// 프레임 기본값 옮기기 판(파일에 없으면 0). 옮기기 규칙을 고칠 때만 올린다.
	constexpr int32 FrameRateDefaultTarget = 2;

	TAutoConsoleVariable<bool> CVarFrameBudgetApplyInPIE(
		TEXT("vb.FrameBudget.ApplyInPIE"), false,
		TEXT("에디터 PIE 에서도 창이 뒤·배터리 프레임 상한을 건다. 기본 끔 - 메뉴 상한은 늘 건다(Design_L8_P02_FrameBudget.md 7절 R4)."));
}

UVBGameUserSettings* UVBGameUserSettings::Get()
{
	return Cast<UVBGameUserSettings>(UGameUserSettings::GetGameUserSettings());
}

void UVBGameUserSettings::SetToDefaults()
{
	Super::SetToDefaults();
	// 엔진이 만든 직후, 기본값으로 되돌리기, 판 불일치 초기화가 이리로 온다.
	SetFrameRateLimit(VBOptionRules::DefaultFrameRateLimit(FPlatformMisc::GetMaxRefreshRate()));
}

void UVBGameUserSettings::LoadSettings(bool bForceReload)
{
	// 클래스를 바꾸면 읽는 구역도 바뀐다. 새 구역이 없으면 옛 엔진 구역에서 먼저 읽어 해상도, 품질을 잃지 않는다.
	// 명령줄 덮어쓰기(-ResX 등)는 그 뒤에 부모가 건다.
	const FString OwnSection = GetClass()->GetPathName();
	const FString EngineSection = UGameUserSettings::StaticClass()->GetPathName();
	bool bChanged = false;
	if (GConfig && !GConfig->DoesSectionExist(*OwnSection, GGameUserSettingsIni)
		&& GConfig->DoesSectionExist(*EngineSection, GGameUserSettingsIni))
	{
		LoadConfig(UGameUserSettings::StaticClass(), *GGameUserSettingsIni);
		bChanged = true;
		VB_LOG(Log, "기계 설정: %s 구역이 없어 %s 에서 옮겨 읽음", *OwnSection, *EngineSection);
	}

	Super::LoadSettings(bForceReload);

	// 무제한(0)만 바꾼다 - 옵션 화면은 이 클래스 전부터 있어서 30, 144 같은 값은 사용자가 고른 것이다.
	if (FrameRateDefaultVersion < FrameRateDefaultTarget)
	{
		if (GetFrameRateLimit() <= 0.0f)
		{
			const int32 RefreshRate = FPlatformMisc::GetMaxRefreshRate();
			VB_LOG(Log, "기계 설정: 프레임 제한 기본값 옮기기 판 %d -> %d: 무제한 -> %.0f (모니터 %d Hz)", FrameRateDefaultVersion,
				FrameRateDefaultTarget, VBOptionRules::DefaultFrameRateLimit(RefreshRate), RefreshRate);
			SetFrameRateLimit(VBOptionRules::DefaultFrameRateLimit(RefreshRate));
		}

		FrameRateDefaultVersion = FrameRateDefaultTarget;
		bChanged = true;
	}

	// 옮긴 결과는 그 자리에서 저장한다 - 에디터는 설정을 두 번 읽어, 저장 전에 다시 읽으면 디스크의 옛 값이 덮는다.
	if (bChanged)
	{
		SaveSettings();
	}

	StartRuntimePolicy();
}

float UVBGameUserSettings::GetEffectiveFrameRateLimit()
{
	const UVBOptionsSettings* Options = GetDefault<UVBOptionsSettings>();
	TArray<float, TInlineAllocator<3>> Caps;
	if (HasMenuFrameRateRequest())
	{
		Caps.Add(Options->MenuFrameRateLimit);
	}

	if (AllowsRuntimeCaps())
	{
		if (!bAppActive)
		{
			Caps.Add(Options->FrameRateLimitWhenBackgrounded);
		}

		if (bOnBattery)
		{
			Caps.Add(Options->FrameRateLimitOnBattery);
		}
	}

	return VBOptionRules::CombineFrameRateLimits(Super::GetEffectiveFrameRateLimit(), Caps);
}

void UVBGameUserSettings::ApplyNonResolutionSettings()
{
	Super::ApplyNonResolutionSettings();
	// 부모가 방금 같은 계산으로 걸었다(전용 서버 빼고) - 다시 걸기의 비교 기준을 맞춘다.
	LastAppliedLimit = GetEffectiveFrameRateLimit();
}

void UVBGameUserSettings::BeginDestroy()
{
	StopRuntimePolicy();
	Super::BeginDestroy();
}

FVBFrameRateRequestHandle UVBGameUserSettings::AddMenuFrameRateRequest(FName Reason)
{
	FVBFrameRateRequestHandle Handle;
	Handle.Id = NextMenuRequestId++;
	MenuRequests.Add(Handle.Id, Reason);
	VB_LOG(Verbose, "기계 설정: 메뉴 프레임 제한 요청 +%s (요청 %d)", *Reason.ToString(), MenuRequests.Num());
	ReapplyFrameRateLimit();
	return Handle;
}

void UVBGameUserSettings::RemoveMenuFrameRateRequest(FVBFrameRateRequestHandle& Handle)
{
	if (!Handle.IsValid())
	{
		return;
	}

	FName Reason;
	if (MenuRequests.RemoveAndCopyValue(Handle.Id, Reason))
	{
		VB_LOG(Verbose, "기계 설정: 메뉴 프레임 제한 요청 -%s (요청 %d)", *Reason.ToString(), MenuRequests.Num());
		// 나갈 때도 기억한 값을 되돌리지 않고 다시 계산한다 - 메뉴 중 옵션 적용이 고른 새 값을 남겨야 한다.
		ReapplyFrameRateLimit();
	}

	Handle = FVBFrameRateRequestHandle();
}

void UVBGameUserSettings::SyncMenuFrameRateRequest(FVBFrameRateRequestHandle& Handle, bool bWant, FName Reason)
{
	if (bWant && !Handle.IsValid())
	{
		Handle = AddMenuFrameRateRequest(Reason);
	}
	else if (!bWant && Handle.IsValid())
	{
		RemoveMenuFrameRateRequest(Handle);
	}
}

void UVBGameUserSettings::StartRuntimePolicy()
{
	if (bRuntimePolicyStarted || IsTemplate() || IsRunningDedicatedServer() || !GEngine || GEngine->GetGameUserSettings() != this)
	{
		return;
	}

	bRuntimePolicyStarted = true;
	bOnBattery = QueryOnBatteryPower();
	TrySubscribeActivation();
	// 전원 바뀜은 알림이 없어 간격마다 본다(코어 티커라 게임이 멈춰도 돈다). 손으로 고친 값(NaN 포함)은 기본 5초로.
	const float PollSeconds = GetDefault<UVBOptionsSettings>()->PowerPollSeconds;
	const float Interval = FMath::IsFinite(PollSeconds) ? FMath::Max(1.0f, PollSeconds) : 5.0f;
	PolicyTicker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateUObject(this, &ThisClass::TickRuntimePolicy), Interval);
	ReapplyFrameRateLimit();
}

void UVBGameUserSettings::StopRuntimePolicy()
{
	FTSTicker::RemoveTicker(PolicyTicker);
	PolicyTicker.Reset();
	if (ActivationHandle.IsValid() && FSlateApplication::IsInitialized())
	{
		FSlateApplication::Get().OnApplicationActivationStateChanged().Remove(ActivationHandle);
	}

	ActivationHandle.Reset();
	bRuntimePolicyStarted = false;
}

bool UVBGameUserSettings::TickRuntimePolicy(float /*DeltaSeconds*/)
{
	// Slate 는 이 설정을 읽은 뒤에 선다 - 서기 전에 시작했으면 여기서 붙는다.
	TrySubscribeActivation();
	const bool bNowOnBattery = QueryOnBatteryPower();
	if (bNowOnBattery != bOnBattery)
	{
		bOnBattery = bNowOnBattery;
		VB_LOG(Log, "기계 설정: 전원 %s", bOnBattery ? TEXT("배터리") : TEXT("연결"));
	}
	// 리슨 호스트가 되는 것처럼 알림 없는 바뀜도 여기서 따라간다 - 값이 같으면 안 건다.
	ReapplyFrameRateLimit();
	return true;
}

void UVBGameUserSettings::TrySubscribeActivation()
{
	if (ActivationHandle.IsValid() || !FSlateApplication::IsInitialized())
	{
		return;
	}

	FSlateApplication& Slate = FSlateApplication::Get();
	ActivationHandle = Slate.OnApplicationActivationStateChanged().AddUObject(this, &ThisClass::HandleActivationChanged);
	// 구독은 지금 상태를 알려 주지 않는다 - 이미 뒤에 있을 수 있다.
	bAppActive = Slate.IsActive();
	ReapplyFrameRateLimit();
}

void UVBGameUserSettings::HandleActivationChanged(bool bIsActive)
{
	bAppActive = bIsActive;
	ReapplyFrameRateLimit();
}

void UVBGameUserSettings::ReapplyFrameRateLimit()
{
	if (IsRunningDedicatedServer())
	{
		return;
	}

	const float Limit = GetEffectiveFrameRateLimit();
	if (Limit == LastAppliedLimit)
	{
		return;
	}

	LastAppliedLimit = Limit;
#if WITH_DEV_AUTOMATION_TESTS
	if (ApplyLimitForTests)
	{
		ApplyLimitForTests(Limit);
		return;
	}
#endif
	SetFrameRateLimitCVar(Limit);
}

bool UVBGameUserSettings::AllowsRuntimeCaps() const
{
#if WITH_DEV_AUTOMATION_TESTS
	if (AllowsRuntimeCapsForTests.IsSet())
	{
		return AllowsRuntimeCapsForTests.GetValue();
	}
#endif
	if (GIsEditor && !CVarFrameBudgetApplyInPIE.GetValueOnGameThread())
	{
		return false;
	}

	if (!GEngine)
	{
		return false;
	}
	// 호스트가 창을 뒤로 보내면 엔진 루프가 느려져 손님이 보는 세계 갱신도 느려진다(7절 R5) - 호스트는 안 건다.
	for (const FWorldContext& Context : GEngine->GetWorldContexts())
	{
		const UWorld* World = Context.World();
		const ENetMode NetMode = World ? World->GetNetMode() : NM_Standalone;
		if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
			&& (NetMode == NM_ListenServer || NetMode == NM_DedicatedServer))
		{
			return false;
		}
	}

	return true;
}

bool UVBGameUserSettings::QueryOnBatteryPower()
{
#if PLATFORM_WINDOWS
	SYSTEM_POWER_STATUS Status;
	const bool bQueryOk = ::GetSystemPowerStatus(&Status) != 0;
	return VBOptionRules::IsOnBatteryPower(bQueryOk, bQueryOk ? Status.ACLineStatus : 255, bQueryOk ? Status.BatteryFlag : 255);
#else
	// 지금 대상은 Windows 데스크톱이다 - 다른 플랫폼은 배터리 상한을 걸지 않는다.
	return false;
#endif
}
