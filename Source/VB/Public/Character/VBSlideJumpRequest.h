// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 미끄럼 중 누른 점프 하나의 요청. 일어선 뒤 이어 주고, 기다림이 끝나거나 지면을 떠나면 버린다.
 * 장면이 캐릭터 제어를 가져가면 전부 버린다. 엔진 상태 값은 캐릭터가 매 Tick 전달하고, 돌아온 행동의 실행도 캐릭터가 한다.
 */
struct VB_API FVBSlideJumpRequest
{
	// 매 Tick 캐릭터가 채우는 입력 값.
	struct FTickInput
	{
		double Now = 0.0;
		bool bSceneAllowed = true; // 요청이 살아 있을 때만 캐릭터가 묻는다(쉬는 동안은 쓰지 않는다)
		bool bHasMovement = false; // 이동 컴포넌트가 있나
		bool bMovingOnGround = false;
		bool bCrouched = false;
		bool bWasJumping = false; // ACharacter::bWasJumping - 이동 갱신이 점프를 썼다
		bool bPressedJump = false; // ACharacter::bPressedJump
		bool bJumpHeld = false; // 캐릭터의 점프 키를 누르고 있음
	};

	// 캐릭터가 이 순서로 적용한다: bClearHeld -> StopJumping -> Jump. bClearHeld 는 장면 취소에서만 온다.
	struct FActions
	{
		bool bClearHeld = false;
		bool bStopJumping = false;
		bool bJump = false;
	};

	// 누름이 '누르고 있음' 을 세우는 자리에서. 앞선 놓음 요청만 지운다.
	void OnPress() { bReleaseRequested = false; }
	// 미끄럼을 끊은 누름이 아직 웅크림일 때 - 대기를 시작한다. 이동 소비 기다림은 뒤 Tick 의 Jump 가 만든다.
	void PressOutOfSlide(double Now, double WindowSeconds) { PendingUntil = Now + WindowSeconds; }
	// 요청이 살아 있으면 놓음 요청만 세우고 true. false 면 캐릭터가 지금 StopJumping 한다.
	bool Release();
	FActions Tick(const FTickInput& In);
	// CanJumpInternal 의 몫: 이동 소비를 기다리는 동안 장면이 막으면 새 점프를 받지 않는다.
	bool BlocksJump(bool bSceneAllowed) const { return bAwaitingMovement && !bSceneAllowed; }
	// 보류 중인 것이 없다(대기도 이동 소비 기다림도 아님). 캐릭터는 이때 Tick 의 요청 단계를 건너뛰고, 놓음은 캐릭터가 바로 처리한다.
	bool IsIdle() const { return PendingUntil <= 0.0 && !bAwaitingMovement; }

private:
	double PendingUntil = 0.0; // 0 = 대기 없음
	bool bAwaitingMovement = false;
	// 요청 중 플레이어가 키를 놓았다. 캐릭터의 '누르고 있음' 은 키를 놓지 않아도 내려가므로 따로 보관한다.
	bool bReleaseRequested = false;

	// 기다림, 대기가 끝날 때: 놓음 요청이 있었고 지금 키를 누르고 있지 않으면 점프를 멈춘다.
	bool TakeRelease(bool bJumpHeld);
};
