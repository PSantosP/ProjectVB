// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

// 공유 스케줄러를 틱하거나 GFrameCounter 를 바꾸지 않고 실제 poll, trace 를 태운다.
//   준비, 빠른 이동 시험이 함께 쓰는 한 정의 - friend class 는 한 모듈에 두 번 정의하면 안 된다.
class FVBPlayerRestoreTestAccess
{
public:
	static void Poll(UVBPlayerRestoreComponent& Restore) { Restore.PollStreaming(); }
	// hold 타이머가 울린 것처럼 - 타이머를 지우고 목표에 놓은 뒤 polling 을 시작한다.
	static void EndHold(UVBPlayerRestoreComponent& Restore)
	{
		if (UWorld* World = Restore.GetWorld())
		{
			World->GetTimerManager().ClearTimer(Restore.HoldTimerHandle);
		}

		Restore.PlaceAndPoll();
	}

	static bool IsHoldPending(const UVBPlayerRestoreComponent& Restore)
	{
		const UWorld* World = Restore.GetWorld();
		return World && World->GetTimerManager().IsTimerActive(Restore.HoldTimerHandle);
	}
};
