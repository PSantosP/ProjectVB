// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"
#include "GameFramework/GameUserSettings.h"
#include "Settings/VBFrameRateRequest.h"
#include "VBGameUserSettings.generated.h"

/**
 * 기계 설정(UGameUserSettings)의 프로젝트판. 프레임 제한 = 모니터 주사율에 메뉴, 창 뒤, 배터리 상한을 합친 값(VBOptionRules::CombineFrameRateLimits).
 * t.MaxFPS 는 GetEffectiveFrameRateLimit 를 묻는 두 경로만 쓴다 - 밖에서 SetMaxFPS 를 부르면 적용 때마다 지워진다.
 * 창 활성, 전원 감시는 엔진이 실제로 쓰는 설정 객체에서만 돈다(StartRuntimePolicy).
 */
UCLASS(config=GameUserSettings, configdonotcheckdefaults)
class VB_API UVBGameUserSettings : public UGameUserSettings
{
	GENERATED_BODY()

public:
	// 엔진 설정이 이 클래스가 아니면 null - 부르는 쪽이 말한다(설정이 깨진 것이다).
	static UVBGameUserSettings* Get();

	virtual void SetToDefaults() override;
	virtual void LoadSettings(bool bForceReload = false) override;
	virtual float GetEffectiveFrameRateLimit() override;
	virtual void ApplyNonResolutionSettings() override;
	virtual void BeginDestroy() override;

	// 메뉴 제한을 원한다(정지 화면이 뜬 UI 관리자, 메인 메뉴 PC). 이유는 로그용. 요청이 하나라도 있으면 메뉴 제한이 걸린다.
	FVBFrameRateRequestHandle AddMenuFrameRateRequest(FName Reason);
	// 그 요청을 거둔다. 손잡이를 비운다. 모르는(이미 거둔) 손잡이는 무시한다.
	void RemoveMenuFrameRateRequest(FVBFrameRateRequestHandle& Handle);
	bool HasMenuFrameRateRequest() const { return MenuRequests.Num() > 0; }
	// 원함이 같은 값으로 여러 번 와도 요청은 하나다: 원하는데 손잡이가 없으면 받고, 안 원하는데 있으면 거둔다(정지 화면 목록이 부른다).
	void SyncMenuFrameRateRequest(FVBFrameRateRequestHandle& Handle, bool bWant, FName Reason);

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBFrameBudgetTestAccess;
	// 시험 주입: 엔진 cvar 대신 여기로 쓴다(사용자의 t.MaxFPS 를 건드리지 않게), 새 상한 판정을 대신한다.
	TFunction<void(float)> ApplyLimitForTests;
	TOptional<bool> AllowsRuntimeCapsForTests;
#endif

	// 엔진 설정 객체일 때만 한 번. 엔진이 객체를 GameUserSettings 에 넣은 뒤 읽으므로 LoadSettings 끝에서 부른다.
	void StartRuntimePolicy();
	void StopRuntimePolicy();
	bool TickRuntimePolicy(float DeltaSeconds);
	void TrySubscribeActivation();
	void HandleActivationChanged(bool bIsActive);
	// 지금 상태로 계산한 값이 마지막으로 건 값과 다를 때만 건다(결과 값 기준, 7절 C1).
	void ReapplyFrameRateLimit();
	// 새 상한 둘(창이 뒤, 배터리)을 이 실행에서 거나: PIE 는 cvar 로만, 리슨 호스트, 전용 서버는 안 건다(7절 R4, R5).
	bool AllowsRuntimeCaps() const;
	static bool QueryOnBatteryPower();

	TMap<int32, FName> MenuRequests;
	int32 NextMenuRequestId = 0;

	bool bRuntimePolicyStarted = false;
	bool bAppActive = true;
	bool bOnBattery = false;
	// 마지막으로 건 값. 엔진 적용 경로가 건 값도 여기에 맞춘다(어긋나면 같은 값이라 안 건다는 판단이 틀린다).
	float LastAppliedLimit = -1.0f;
	FTSTicker::FDelegateHandle PolicyTicker;
	FDelegateHandle ActivationHandle;

	// 프레임 제한 기본값 옮기기의 판. 파일 판이 FrameRateDefaultTarget 보다 낮으면 한 번 옮긴다(엔진 기본 0 만 주사율로 바꾼다).
	// bool 이 아닌 이유: 옮기기 규칙을 고치면 판을 올려 다시 옮길 수 있어야 한다.
	UPROPERTY(config)
	int32 FrameRateDefaultVersion = 0;
};
