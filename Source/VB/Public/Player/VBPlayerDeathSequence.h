// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"

class APlayerController;
class FVBPlayerScreens;

// 사망 연출의 조정값. 값은 AVBPlayerController 의 설정 필드가 관리한다.
struct FVBPlayerDeathSettings
{
	float SlowMoScale = 0.3f;
	float SlowMoRealDuration = 0.9f;
	float ScreenExtraDelay = 0.5f;
	float MinScreenDelay = 1.0f;
};

// 플레이어 사망 표시를 맡는다: 슬로모, HUD 숨김, 게임오버 화면 예약, 이어하기 로드 대기. 사망 판정은 맡지 않는다.
class FVBPlayerDeathSequence
{
public:
	// 사망 연출 중이면 true - 게임오버 화면까지 일시정지를 막는다.
	bool IsActive() const { return bActive; }

	// 소유 폰 사망(로컬 조종일 때만): 슬로모, HUD 숨김, 게임오버 화면 예약. 겹쳐 불러도 한 번만 돈다.
	void Begin(APlayerController& Controller, FVBPlayerScreens& Screens, const FVBPlayerDeathSettings& Settings, float DeathAnimSeconds);

	// "계속하기" 비동기 로드 진행 중이면 true. 중복 클릭으로 로드가 2회 겹치는 것을 막는다.
	bool IsContinueInProgress() const { return bContinueInProgress; }
	void SetContinueInProgress(bool bInProgress) { bContinueInProgress = bInProgress; }

private:
	bool bActive = false;
	bool bContinueInProgress = false;

	// 슬로모 종료 -> 게임오버 화면 타이머.
	FTimerHandle GameOverTimerHandle;
};
