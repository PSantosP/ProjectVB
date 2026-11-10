// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGameplayAbility.h"
#include "VBGA_Guard.generated.h"

/**
 * 가드/패링 어빌리티(누르는 동안 유지). 피해 허브가 Guarding 태그 + 가드 경과 시각으로 패링/블록을 가른다.
 * 서버에서 BeginGuard / EndGuard 를 부른다. 무기 게이트는 BeginGuard 가 아니라 CanActivateAbility 에 둔다(Guarding 태그가 남는다).
 */
UCLASS(Abstract)
class VB_API UVBGA_Guard : public UVBGameplayAbility
{
	GENERATED_BODY()
public:
	UVBGA_Guard();

	// 무기 세트가 준 가드(SourceObject = 무기 정의)만 켜진다. 켜는 순간 공격을 끊으므로 켜기 전에 묻는다.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override;

protected:
	// 가드를 올리는 순간 카메라 기준으로 자동 락온할지. 이미 락온 중이면 무시한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Guard")
	bool bAutoLockOnGuard = true;

private:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;
};
