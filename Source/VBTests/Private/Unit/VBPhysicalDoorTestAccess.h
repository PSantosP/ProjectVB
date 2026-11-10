// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "Interaction/VBPhysicalDoor.h"
#include "Narrative/Story/VBAreaTrigger.h"

// 월드 tick 없이 물리 문을 움직인다 - 이동이 Seconds 전에 시작된 것으로 두고 문 tick 을 한 번 돌린다
// 문, 전원 패널 시험이 같이 쓰는 정의 하나다 (friend class 는 한 모듈에 두 번 정의하면 안 된다)
class FVBPhysicalDoorTestAccess
{
public:
	static void Advance(AVBPhysicalDoor& Door, double Seconds)
	{
		Door.Move.ServerStartTime -= Seconds;
		static_cast<AActor&>(Door).Tick(0.016f);
	}

	static float LeafAlpha(const AVBPhysicalDoor& Door) { return Door.LastPlacedAlpha; }

	// 이야기 닫힘은 배치 값이라 BeginPlay 전에 넣는다.
	static void ConfigureStory(AVBPhysicalDoor& Door, const TArray<FInstancedStruct>& ClosedWhile, AVBAreaTrigger* Area)
	{
		Door.ClosedWhile = ClosedWhile;
		Door.ClosedWhileOccupied = Area;
	}

	static bool StoryStarted(const AVBPhysicalDoor& Door) { return Door.bStoryStarted; }
	static bool RetryPending(const AVBPhysicalDoor& Door) { return Door.bBlockedRetryPending; }
	// 이 프레임에 건 타이머는 FTimerManager::Tick 한 번으로 돌지 않아 처리기를 직접 부른다.
	static void FireRetry(AVBPhysicalDoor& Door) { Door.HandleBlockedRetry(); }
	static void ExpireStoryStartup(AVBPhysicalDoor& Door)
	{
		Door.StoryStartupDeadline = -1.0;
		Door.HandleStoryStartupTick();
	}
};
