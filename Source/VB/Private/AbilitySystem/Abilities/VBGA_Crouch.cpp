// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Crouch.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "GameFramework/Character.h"

UVBGA_Crouch::UVBGA_Crouch()
{
	InputTag = VBGameplayTags::Input_Crouch;
	bIsToggleAbility = true;
	
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Locomotion_Crouch));
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Locomotion_Crouching);
}

void UVBGA_Crouch::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	ACharacter* Char = GetAvatarCharacter();
	if (!Char)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Crouch 는 CMC 네트워크 예측을 쓰므로 서버와 클라 양쪽에서 호출한다(클라가 안 부르면 ABP 전환이 실패한다).
	Char->Crouch();
}

void UVBGA_Crouch::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// UnCrouch도 양쪽 호출 필요 (CMC prediction 대칭)
	if (ACharacter* Char = GetAvatarCharacter())
	{
		Char->UnCrouch();
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
