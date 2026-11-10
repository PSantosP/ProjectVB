// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Abilities/GameplayAbilityTypes.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "VBGA_UseItem.generated.h"

class UAnimMontage;
class UVBItemDefinition;

/**
 * 몽타주가 있는 아이템 사용. Event.Item.Use 로 켜지고, 표식(Event.Item.UseMarker) 시각에 서버가 ConfirmUse 로 확정한다.
 * 표식 전 피격이면 취소, 회피, 가드가 켜지면 끝낸다 - 표식 전에 끊기면 소모하지 않는다. 끝내는 것은 서버다.
 */
UCLASS()
class VB_API UVBGA_UseItem : public UVBGameplayAbility
{
	GENERATED_BODY()

public:
	UVBGA_UseItem();

	// 매달림, 등반, 트래버설 중이거나 공중이면 시작하지 않는다. 입력 관문을 지나지 않아 여기서 직접 거른다.
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags,
		FGameplayTagContainer* OptionalRelevantTags) const override;

	// 요청의 아이템을 지금 쓸 수 있나. 활성화 전에 걸러야 클라가 몽타주를 한순간 틀었다 끊지 않는다.
	virtual bool ShouldAbilityRespondToEvent(const FGameplayAbilityActorInfo* ActorInfo, const FGameplayEventData* Payload) const override;

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

private:
	UFUNCTION()
	void HandleMarker(FGameplayEventData Payload);

	UFUNCTION()
	void HandleHit(FGameplayEventData Payload);

	UFUNCTION()
	void HandleDefenseActivated(UGameplayAbility* ActivatedAbility);

	UFUNCTION()
	void HandleMontageEnded();

	// 서버에서만 끝낸다. 이유 = 클래스 주석.
	void EndOnServer(bool bWasCancelled, const TCHAR* Why);

	// 아이템이 원하면 든 무기를 임시 수납한다(서버). State.Weapon.TempSheathed 로 소환 GA 를 막는다.
	void SheatheIfWanted(const struct FVBItemFragment_Usable& Usable);

	// 이번 활성화가 무기를 수납했나. 끝날 때 우리 몫만 돌려놓는다.
	bool bSheathedWeapon = false;

	// 이번 활성화의 아이템과 몽타주. 표식 때 내 몽타주가 아직 도는지 이것으로 본다.
	UPROPERTY()
	TObjectPtr<const UVBItemDefinition> ActiveItem;

	UPROPERTY()
	TObjectPtr<UAnimMontage> ActiveMontage;

	// 표식이 왔다. 이후의 표식, 피격으로는 다시 확정, 취소하지 않는다.
	bool bMarkerReached = false;
};
