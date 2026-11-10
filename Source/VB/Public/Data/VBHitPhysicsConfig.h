// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "PhysicsEngine/PhysicalAnimationComponent.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBHitPhysicsConfig.generated.h"

// 등급 하나의 물리 피격 세기와 시간. 블렌드는 0 -> MaxBlendWeight(RiseSeconds) -> 0(FallSeconds).
USTRUCT(BlueprintType)
struct VB_API FVBHitPhysicsTier
{
	GENERATED_BODY()

	// 맞은 뼈에 주는 속도 변화(cm/s). 질량과 무관하게 같은 출렁임이 나도록 속도로 준다. 0 이면 이 등급은 물리 반응이 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float ImpulseSpeed = 0.0f;

	// 물리가 몽타주 위에 섞이는 최대 비율. 1 에 가까울수록 몽타주가 안 보인다 - 동작 표현의 중심은 몽타주라 상한을 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MaxBlendWeight = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.0"))
	float RiseSeconds = 0.03f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.01"))
	float FallSeconds = 0.3f;

	bool IsActive() const { return ImpulseSpeed > 0.0f && MaxBlendWeight > 0.0f; }

	// 시작 뒤 Elapsed 초의 블렌드 가중치. 끝났으면 0.
	float EvaluateBlend(float Elapsed) const;
	float TotalSeconds() const { return RiseSeconds + FallSeconds; }
};

/**
 * 서 있는 캐릭터의 물리 피격 층 설정. UVBHitPhysicsComponent 가 읽는다.
 * 몽타주가 무엇을 당했나를, 이 층이 어디를 얼마나 세게를 맡아 등급은 피격 몽타주와 같은 EVBHitImpact 다.
 */
UCLASS(BlueprintType)
class VB_API UVBHitPhysicsConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 이 뼈와 그 아래만 시뮬레이션한다(골반, 다리는 캡슐과 루트모션 몫).
	// 뼈가 아니라 물리 자산의 바디여야 한다 - 바디가 없으면 층 전체가 꺼진다(ApplyHit 이 경고).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FName SimulateRootBone = TEXT("spine_02");

	// 맞은 뼈를 모르거나 시뮬레이션 범위 밖이면 여기에 충격을 준다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FName FallbackHitBone = TEXT("spine_03");

	// 시뮬레이션하는 뼈가 애니 자세로 돌아가려는 힘. 세면 출렁임이 짧고, 약하면 흐느적거린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FPhysicalAnimationData PhysicalAnimation;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	TMap<EVBHitImpact, FVBHitPhysicsTier> Tiers;

	// 이 태그 중 하나라도 있으면 물리 반응을 하지 않는다(예: 공중 루프, 슈퍼아머 연출).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Combat")
	FGameplayTagContainer BlockedTags;

	// 등급의 설정. 없거나 꺼져 있으면 nullptr.
	const FVBHitPhysicsTier* FindActiveTier(EVBHitImpact Impact) const;
};
