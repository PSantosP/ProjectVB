// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"

class ACharacter;

// 회복 캔슬 직후 이동 상태를 기록하는 진단(vb.Combat.RecoveryCancelTrace 1). GA 가 타이머를 가지면 EndAbility 가 지우므로 캐릭터가 값 멤버로 보관한다.
class VB_API FVBRecoveryCancelTrace
{
public:
	// CVar 가 켜져 있으면 표본 기록을 처음부터 다시 시작한다. Body 는 이 값을 멤버로 가진 캐릭터다.
	void Begin(ACharacter& Body);

private:
	// 추적 타이머와 남은 표본 수.
	FTimerHandle Timer;
	int32 SamplesLeft = 0;
};
