// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayCueNotify_Static.h"
#include "VBGC_HitPhysics.generated.h"

/**
 * 물리 피격 층을 깨우는 큐. 피격자의 UVBHitPhysicsComponent 에 위임한다.
 * 파라미터: Normal = 미는 방향, RawMagnitude = 실효 등급, HitResult = 맞은 뼈. 태그는 BP 자식이 든다.
 */
UCLASS()
class VB_API UVBGC_HitPhysics : public UGameplayCueNotify_Static
{
	GENERATED_BODY()

public:
	virtual bool OnExecute_Implementation(AActor* MyTarget, const FGameplayCueParameters& Parameters) const override;
};
