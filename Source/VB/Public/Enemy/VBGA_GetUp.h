// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_GetUp.generated.h"

class UAnimMontage;

/**
 * 처형을 맞고도 죽지 않은 대상이 일어난다. Event.Combat.FinisherSurvived(UVBGA_Execute 가 쏜다)로 트리거된다.
 * 래그돌이면 VBRagdollConfig 의 Chooser 로 물리 포즈에 맞는 기상을, 아니면 고정 몽타주를 쓰러진 방향에 맞춰 튼다.
 * 몽타주가 없으면 즉시 끝낸다.
 */
UCLASS(Abstract)
class VB_API UVBGA_GetUp : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_GetUp();

protected:
	// 베이스가 ActivateAbility 를 final 로 잠그고 CommitAbility 를 강제하므로 여기를 오버라이드한다.
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

private:
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 기상할 몽타주가 없을 때 기상 상태를 닫고 끝낸다.
	void EndWithoutMontage(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo);

	// 기상 몽타주 태스크를 시작한다. 태스크가 동기로 취소하면 이 안에서 어빌리티가 끝날 수 있다.
	void PlayGetUpMontage(const UAnimMontage& Montage, double StartTime);

	// 완료/중단/재생 실패는 종료한다. 블렌드아웃 시작만으로는 기상 상태를 풀지 않는다(물리 정리는 EndAbility).
	UFUNCTION()
	void OnGetUpEnded();
};
