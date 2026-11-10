// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/Abilities/VBTelegraphGrant.h"
#include "VBGA_BossThrow.generated.h"

class UVBProjectileConfig;

/**
 * 보스가 전투 표적에 투사체를 던진다. 몽타주의 놓는 노티(Event.Montage.ProjectileRelease)가 놓는 시각을 정한다.
 * 감는 동안 예고 큐를 세우고, 놓을 때 타격 스펙을 만들어 투사체에 실어 보낸다. 피해는 투사체가 맞은 순간 서버에서 건다.
 */
UCLASS(Abstract)
class VB_API UVBGA_BossThrow : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_BossThrow();

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		const FGameplayEventData* TriggerEventData) override;

	// 예고를 걷는다. 취소로 들어와도 여기로 모인다.
	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle,
		const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo,
		bool bReplicateEndAbility,
		bool bWasCancelled) override;

	// 던질 투사체의 설정.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	TObjectPtr<const UVBProjectileConfig> ProjectileConfig;

	// 투사체가 나가는 손 소켓. 메시에 없으면 던지지 않고 오류를 남긴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Combat")
	FName ReleaseSocketName;

private:
	// 놓는 노티가 왔다 - 스펙을 만들고 투사체를 띄운다. 서버 전용.
	UFUNCTION()
	void OnRelease(FGameplayEventData Payload);

	// 몽타주가 정상으로 끝났다.
	UFUNCTION()
	void OnMontageFinished();

	// 몽타주가 끊겼다(피격, 경직, 취소).
	UFUNCTION()
	void OnMontageCut();

	FVBTelegraphGrant Telegraph;

	// 이 활성화가 이미 던졌는가. 몽타주가 노티 없이 끝나면 저작 오류로 알린다.
	bool bReleased = false;
};
