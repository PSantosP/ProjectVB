// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBOnlineSession.h"

#include "VB/VB.h"

void UVBOnlineSession::HandleDisconnect(UWorld* World, UNetDriver* NetDriver)
{
	// 막힌 동안 엔진은 끊긴 연결을 매 프레임 다시 실패로 알린다(UWorld::TickNetClient). 그때마다 여기로 온다.
	if (!bDisconnectHeld)
	{
		Super::HandleDisconnect(World, NetDriver);
	}
}

bool UVBOnlineSession::TryHoldDisconnect()
{
	if (bDisconnectHeld)
	{
		return false;
	}
	bDisconnectHeld = true;
	VB_LOG(Log, "[연결 알림] 확인할 때까지 메뉴 이동을 막는다");
	return true;
}

void UVBOnlineSession::ReleaseDisconnect()
{
	bDisconnectHeld = false;
}
