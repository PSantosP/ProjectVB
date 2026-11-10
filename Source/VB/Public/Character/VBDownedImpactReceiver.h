// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBDownedImpactReceiver.generated.h"

/**
 * 물리로 쓰러진 캐릭터가 타격을 받는 계약. 피해 허브가 적 클래스를 Cast 하지 않고 넉백 대신 충격으로 전환한다.
 * 허브는 넉백과 같은 방향, 등급, 배수만 전달하고 판단은 받는 쪽이 한다. 서버에서만 부른다.
 */
UINTERFACE(MinimalAPI, meta=(CannotImplementInterfaceInBlueprint))
class UVBDownedImpactReceiver : public UInterface
{
	GENERATED_BODY()
};

class IVBDownedImpactReceiver
{
	GENERATED_BODY()

public:
	// 지금 캐릭터가 물리로 쓰러져 있어 이 타격의 이동을 물리 바디가 받는가. 참이면 허브는 넉백을 걸지 않고 ReceiveDownedImpact 를 부른다.
	virtual bool IsBodyDownedInPhysics() const = 0;

	// 맞은 자리(Hit - 판정이 준 뼈, 접점, 없으면 nullptr)에 밀린 방향으로 충격을 준다.
	// 반환 = 실제로 준 속도 충격의 크기(cm/s). 상한이 지났거나 그 등급이 0 이면 0 - 허브 로그가 이 값을 찍는다.
	virtual float ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale) = 0;
};
