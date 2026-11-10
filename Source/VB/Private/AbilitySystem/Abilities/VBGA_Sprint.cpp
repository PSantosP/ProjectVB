// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Sprint.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBLocomotionIntentComponent.h"

UVBGA_Sprint::UVBGA_Sprint()
{
	InputTag = VBGameplayTags::Input_Sprint;
	bCancelOnInputRelease = true; // 누르고 있는 동안만 달린다 - 놓으면 끝
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Locomotion_Sprint));
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Locomotion_Sprinting);
}

void UVBGA_Sprint::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!Intent)
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// StartSprint 는 서버 RPC 다. 서버에서 부르면 바로 실행된다.
	// ServerInitiated로 클라도 ExecuteAbility 실행하므로 중복 호출 방지를 위해 권한 가드.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		Intent->StartSprint();
	}
}

void UVBGA_Sprint::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	// EndAbility는 양쪽에서 호출될 수 있음(timer, cancel, explicit end). 서버에서만 실제 sprint 토글.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			Intent->StopSprint();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
