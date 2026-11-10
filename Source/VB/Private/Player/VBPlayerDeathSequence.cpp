// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Player/VBPlayerDeathSequence.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/PlayerController.h"
#include "Player/VBPlayerScreens.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "TimerManager.h"
#include "UI/VBUIManagerSubsystem.h"
#include "VB/VB.h"

namespace
{
	// 사망 슬로모 요청 핸들 - Push 와 Pop 이 같은 이름을 써야 짝이 맞아 한 곳에 둔다.
	const FName GDeathSlowMoHandle = TEXT("PlayerDeath");

	// 슬로모 종료 -> 게임오버 화면 요청(정지하지 않는다).
	void ShowGameOverMenu(const APlayerController& Controller)
	{
		if (!Controller.IsLocalController())
		{
			return;
		}

		// 슬로모는 이미 만료됐을 것이다 - Pop 은 남아 있을 때를 위한 안전망.
		if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(&Controller))
		{
			TimeDilation->Pop(GDeathSlowMoHandle);
		}

		// 게임을 멈추지 않는다(화면 데이터 bPausesGame=false). 입력은 화면의 Menu 입력 모드, 폰과 PC 의 DisableInput, 사망 연출 상태가 막는다.
		if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
		{
			UI->PushScreen(VBGameplayTags::UI_Screen_GameOver);
		}
		else
		{
			// 조용한 실패 금지: 화면이 없으면 조작 불능인 시체만 남으므로 원인을 로그로 남긴다.
			VB_LOG(Warning, "[VBPC] UI 관리자를 못 찾았다 — 게임오버 화면이 뜨지 않습니다.");
		}
	}
}

void FVBPlayerDeathSequence::Begin(APlayerController& Controller, FVBPlayerScreens& Screens, const FVBPlayerDeathSettings& Settings,
	float DeathAnimSeconds)
{
	// 로컬 전용 - 호출자가 이미 걸렀지만 진입점이 스스로도 막는다.
	if (!Controller.IsLocalController())
	{
		return;
	}

	if (bActive)
	{
		return; // 같은 프레임 다중 킬링 GE 등으로 사망 통지가 겹쳐도 연출은 1회만.
	}

	bActive = true;
	Screens.CloseSelectionScreens(Controller);

	// PC 자신의 입력도 끊는다(폰의 DisableInput 은 PC 묶음을 못 끈다). 일시정지는 HUD 가 받으므로 사망 연출 상태가 막는다.
	Controller.DisableInput(&Controller);

	// HUD 층을 감춘다(내리지 않는다): HUD 는 입력의 바닥이고, Collapsed 는 히트테스트, 틱에서 빠지며, 부활 때 한 줄로 되돌린다.
	if (UVBUIManagerSubsystem* UI = UVBUIManagerSubsystem::Get(&Controller))
	{
		UI->SetLayerHidden(VBGameplayTags::UI_Layer_Game, true);
	}

	// 사망 직후 짧은 슬로모(PushTimed - 서브시스템이 실시간 만료를 계산한다).
	if (UVBTimeDilationSubsystem* TimeDilation = UVBTimeDilationSubsystem::Get(&Controller))
	{
		TimeDilation->PushTimed(GDeathSlowMoHandle, Settings.SlowMoScale, Settings.SlowMoRealDuration);
	}

	// 게임오버 화면은 사망 애니가 끝난 뒤(게임초 타이머 - 슬로모가 얼마든 맞는다). 클립이 없으면 최소 길이를 쓴다.
	const float AnimSeconds = FMath::Max(DeathAnimSeconds, Settings.MinScreenDelay);
	APlayerController* ControllerPtr = &Controller;
	Controller.GetWorldTimerManager().SetTimer(
		GameOverTimerHandle,
		FTimerDelegate::CreateWeakLambda(ControllerPtr, [ControllerPtr]() { ShowGameOverMenu(*ControllerPtr); }),
		AnimSeconds + Settings.ScreenExtraDelay,
		false);
}
