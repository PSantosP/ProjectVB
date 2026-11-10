// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBSlideJumpRequest.h"

bool FVBSlideJumpRequest::Release()
{
	if (IsIdle())
	{
		return false;
	}

	bReleaseRequested = true;
	return true;
}

bool FVBSlideJumpRequest::TakeRelease(bool bJumpHeld)
{
	const bool bStop = bReleaseRequested && !bJumpHeld;
	bReleaseRequested = false;
	return bStop;
}

FVBSlideJumpRequest::FActions FVBSlideJumpRequest::Tick(const FTickInput& In)
{
	// 순서가 계약이다(설계 9.1 전이 표): 장면 -> 이동 소비 기다림 -> 대기. 기다림 소비가 대기보다 앞이라 같은 Tick 에 새로 만든
	//   기다림을 소비하지 않는다.
	FActions Out;
	if (IsIdle())
	{
		return Out;
	}

	if (!In.bSceneAllowed)
	{
		// 장면이 캐릭터를 점유하면 대기 중인 점프, 키 뗌 요청, 누름 유지 상태까지 버린다 - 장면이 끝나도 다시 내지 않는다.
		PendingUntil = 0.0;
		bAwaitingMovement = false;
		bReleaseRequested = false;
		Out.bClearHeld = true;
		Out.bStopJumping = true;
		return Out;
	}

	if (bAwaitingMovement && (In.bWasJumping || !In.bPressedJump || !In.bHasMovement || !In.bMovingOnGround))
	{
		bAwaitingMovement = false;
		Out.bStopJumping |= TakeRelease(In.bJumpHeld);
	}

	if (PendingUntil > 0.0)
	{
		if (In.Now >= PendingUntil || !In.bHasMovement || !In.bMovingOnGround)
		{
			PendingUntil = 0.0;
			Out.bStopJumping |= TakeRelease(In.bJumpHeld);
		}
		else if (!In.bCrouched)
		{
			// 일어섰다 - 이제 점프를 내고, 이동 갱신이 그것을 쓸 때까지 기다린다.
			PendingUntil = 0.0;
			bAwaitingMovement = true;
			Out.bJump = true;
		}
	}

	return Out;
}
