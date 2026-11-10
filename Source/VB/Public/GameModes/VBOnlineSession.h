// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/OnlineSession.h"
#include "VBOnlineSession.generated.h"

/**
 * 세션을 떠나는 모든 엔진 경로가 지나는 문이다. 엔진은 실패 다음 프레임에 기본 맵으로 옮기므로,
 * 실패 팝업이 떠 있는 동안 여기서 그 이동을 막아 플레이어가 확인한 뒤에 떠나게 한다.
 */
UCLASS()
class VB_API UVBOnlineSession : public UOnlineSession
{
	GENERATED_BODY()

public:
	virtual void HandleDisconnect(UWorld* World, UNetDriver* NetDriver) override;

	// 이미 막혀 있으면 false 다. 끊김 한 번에 실패가 둘 와도 팝업은 먼저 막은 쪽 하나만 뜬다.
	bool TryHoldDisconnect();
	void ReleaseDisconnect();
	bool IsDisconnectHeld() const { return bDisconnectHeld; }

private:
	bool bDisconnectHeld = false;
};
