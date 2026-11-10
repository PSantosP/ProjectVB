// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBDeathFacts.generated.h"

/** 치명타 뒤 실제 착지까지 이어지는 이동과 충격 */
USTRUCT()
struct VB_API FVBDeathMotion
{
	GENERATED_BODY()
	UPROPERTY() FVector InitialVelocity = FVector::ZeroVector;
	UPROPERTY() float MaxAirSeconds = 0.0f;
	UPROPERTY() FGameplayTag GroundImpactCue;

	bool IsValid() const
	{
		return !InitialVelocity.ContainsNaN() && FMath::IsFinite(InitialVelocity.SizeSquared())
			&& InitialVelocity.Z < 0.0f && FMath::IsFinite(MaxAirSeconds) && MaxAirSeconds > 0.0f;
	}
};

/**
 * 한 번의 죽음에 대한 사실. 체력 세트가 0 을 넘는 그 쓰기에서 만들고(UVBHealthAttributeSet::OnOutOfHealth),
 *   UVBHealthComponent 가 캐릭터의 단계(IVBDeathSteps)와 두 알림(OnDeathCompleted, 버스 Message.Death)에 같은 값을 전달한다.
 * 동기로만 건넨다 - 받는 쪽은 보관하지 않는다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBDeathFacts
{
	GENERATED_BODY()

	// 죽은 캐릭터 = 체력 세트를 가진 ASC 의 아바타.
	UPROPERTY(BlueprintReadOnly, Category="VB|Death")
	TObjectPtr<AActor> DeadActor = nullptr;

	// 죽인 자 = 그 쓰기를 일으킨 효과 컨텍스트의 Instigator(플레이어면 PlayerState).
	//   효과 밖의 쓰기(저장 복원, 디버그 즉사 키)는 null 이다 - "죽인 자 없음" 이 정책이다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Death")
	TObjectPtr<AActor> Instigator = nullptr;

	// 처형, 정화 피해로 죽었나(UVBAbilitySystemStatics::IsFinisherDamage). 참이면 적의 기본 사망 명성만 건너뛴다 - 처치 사건은 그대로.
	UPROPERTY(BlueprintReadOnly, Category="VB|Death")
	bool bMedical = false;

	// 죽는 순간 엎드려 있었나(State.Downed). 능력 취소가 그 태그를 걷기 전에 UVBHealthComponent 가 찍는다 -
	//   사망 표시가 엎드린 자세를 이어 갈지 이것으로 고른다(서 있는 사망 클립을 엎드린 자세에서 틀면 메시가 튄다).
	UPROPERTY(BlueprintReadOnly, Category="VB|Death")
	bool bDownedAtDeath = false;
	UPROPERTY(BlueprintReadOnly, Category="VB|Death")
	bool bAirborneAtDeath = false;
	UPROPERTY()
	FVBDeathMotion Motion;
};
