// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "Player/VBPlayerController.h"
#include "VBVibrationProbeController.generated.h"

// 진동 시험(VB.Unit.Options.Vibration)이 스폰할 PC. 엔진은 햅틱 끄기 값(bDisableHaptics)을 private 비트로 숨기고 읽는 함수도 없어서,
//   SetDisableHaptics 로 받은 값을 그대로 기록한다 - 동작은 베이스 그대로.
UCLASS()
class AVBVibrationProbeController : public AVBPlayerController
{
	GENERATED_BODY()

public:
	TOptional<bool> LastDisableHaptics;

	virtual void SetDisableHaptics(bool bNewDisabled) override
	{
		LastDisableHaptics = bNewDisabled;
		Super::SetDisableHaptics(bNewDisabled);
	}
};
