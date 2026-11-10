// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Types/VBWeaponStateTypes.h"

class UCharacterMovementComponent;

// MM 풀이 바뀔 때 검색을 언제 끊을지 정하는 래치. UVBAnimInstance 가 값으로 들고 게임 스레드에서만 쓴다.
struct VB_API FVBAnimPoolInterruptLatch
{
	// 한 프레임의 판정 입력.
	struct FInputs
	{
		float DeltaSeconds = 0.0f;
		const UCharacterMovementComponent* Movement = nullptr;
		float Speed2D = 0.0f;
		EVBWeaponType Pool = EVBWeaponType::None;
		EVBWeaponType PoolLastFrame = EVBWeaponType::None;
		EVBLocomotionState LocomotionState = EVBLocomotionState::Unarmed;
		// 속도가 목표 최대속도에 이만큼 붙으면 보폭이 수렴한 것으로 본다(cm/s).
		float SpeedSettleTolerance = 0.0f;
		// 이 속도 이하면 수렴으로 본다(cm/s).
		float NearStopSpeed = 0.0f;
		// 수렴을 못 봐도 이 시간이 지나면 발화한다(초).
		float TimeoutSeconds = 0.0f;
	};

	// 이번 프레임에 발화해야 하면 true. 바꾸면 BigSword 발도/납도 느낌을 다시 확인한다.
	bool Step(const FInputs& In);

private:
	// 풀 교체 대기 중인가. 보폭이 수렴한 뒤 한 번 발화해야 클립을 두 번 갈아타지 않는다.
	bool bPending = false;
	// 대기 경과(초).
	float PendingTime = 0.0f;
	// 전 프레임 납도 풀 여부. 발도<->납도는 무기 종류가 같아 풀 변경으로 안 잡히므로 따로 즉시 끊는다.
	bool bSheathedLastFrame = false;
};
