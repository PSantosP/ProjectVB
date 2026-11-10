// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "RootMotionModifier_SkewWarp.h"
#include "VBRootMotionModifier_ScaledSkewWarp.generated.h"

/**
 * 메시 배율을 보정하는 Skew Warp. 엔진 SkewWarp 는 배율이 1 이 아니면 속도 곡선이 휘어 캐릭터가 미끄러진다.
 * 부모를 부르는 동안만 목표, 시작 위치를 1/배율로 당겨 거리를 애니 단위로 맞춘다. 배율 1 이면 부모와 같다.
 * 배율이 1 이 아닌 캐릭터의 몽타주는 이 모디파이어를 써야 한다.
 */
UCLASS(meta = (DisplayName = "Skew Warp (메시 배율 보정)"))
class VB_API UVBRootMotionModifier_ScaledSkewWarp : public URootMotionModifier_SkewWarp
{
	GENERATED_BODY()

public:
	UVBRootMotionModifier_ScaledSkewWarp(const FObjectInitializer& ObjectInitializer);

	virtual FTransform ProcessRootMotion(const FTransform& InRootMotion, float DeltaSeconds) override;
};
