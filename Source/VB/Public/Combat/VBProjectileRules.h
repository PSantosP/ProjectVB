// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 던지기 조준의 순수 계산. 월드, 액터, 설정을 모르고 숫자만 받는다 - 던지는 능력이 값을 모아 부른다.
namespace VBProjectileRules
{
	// 비행 시간(거리 / 속력) 동안 표적이 갈 만큼 앞을 겨눈 점. 앞선 거리는 MaxLeadDistance 로 자르고, 속력이 0 이하면 표적 점.
	VB_API FVector AimPoint(const FVector& TargetLocation, const FVector& TargetVelocity, const FVector& Origin,
		float Speed, float MaxLeadDistance);

	// Origin 에서 AimPoint 로 Speed 만큼 날아가는 발사 속도. 같은 비행 시간에 중력이 떨어뜨릴 높이만큼 위로 더한다.
	//   GravityZ 는 이 투사체에 걸리는 중력(보통 음수). 속력이 0 이하이거나 두 점이 같으면 0.
	VB_API FVector LaunchVelocity(const FVector& Origin, const FVector& AimPoint, float Speed, float GravityZ);
}
