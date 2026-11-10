// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBAnimationTypes.generated.h"

UENUM(BlueprintType)
enum class EVBMMState : uint8
{
	// GASP 와 같이 Moving = 현재, 미래 속도가 모두 0 이 아님. 시작/정지 분기는 chooser 가 맡는다.
	Idle UMETA(DisplayName = "Idle"),
	Moving UMETA(DisplayName = "Moving"),
};

// 입력으로 정하는 Gait. 기본 Run, Walk 토글이면 Walk, Sprint 버튼이 가장 우선.
UENUM(BlueprintType)
enum class EVBGait : uint8
{
	Walk UMETA(DisplayName = "Walk"),
	Run UMETA(DisplayName = "Run"),
	Sprint UMETA(DisplayName = "Sprint"),
};

// GASP E_MovementMode 대응. ABP 의 switch 분기용.
UENUM(BlueprintType)
enum class EVBMovementMode : uint8
{
	Grounded UMETA(DisplayName = "Grounded"),
	InAir UMETA(DisplayName = "In Air"),
};

// GASP E_RotationMode 대응. 스트레이프, 락온, 무장 상태를 합친 회전 방식.
UENUM(BlueprintType)
enum class EVBRotationMode : uint8
{
	OrientToMovement UMETA(DisplayName = "Orient To Movement"), // Free: 이동 방향으로 회전
	Strafe UMETA(DisplayName = "Strafe"), // Strafe: 카메라 방향 유지
	OrientToTarget UMETA(DisplayName = "Orient To Target"), // LockOn: 타겟 방향 유지
};

// GASP E_Stance 대응.
UENUM(BlueprintType)
enum class EVBStance : uint8
{
	Stand UMETA(DisplayName = "Stand"),
	Crouch UMETA(DisplayName = "Crouch"),
};