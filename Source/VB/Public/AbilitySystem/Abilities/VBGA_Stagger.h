// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_Stagger.generated.h"

class UVBHitReactConfig;

/**
 * 경직(기절 게이지 파탄) 어빌리티. Event.Combat.PoiseBroken 으로 피격자 ASC 에서 켜져 경직 몽타주를 재생하고 공격을 막는다.
 * 토큰 해제는 공격 GA 가 끝나며 Attacking 태그가 빠질 때 UVBEnemyCombatComponent 가 한다.
 */
UCLASS(Abstract)
class VB_API UVBGA_Stagger : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_Stagger();

protected:
	// 경직 데이터(피격자 소유). 히트리액트와 같은 자산을 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<UVBHitReactConfig> HitReactConfig;

	// 파생은 이 함수를 건너뛰지 못한다. 반응을 바꾸려면 RunReaction 을 재정의한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override final;

	// 이 능력의 반응 본문. 기본은 서 있는 경직이다.
	virtual void RunReaction(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData);

	// 처형 창을 받는 적(PostureExecutable)이거나 창이 이미 켜졌으면 게이지 소진을 받지 않는다. 넘어뜨리는 타격은 거르지 않는다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayEventData* Payload) const override;

private:
	// 몽타주 종료 4경로 공통 종료.
	UFUNCTION()
	void OnStaggerEnded();
};
