// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Guard.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBGuardComponent.h"
#include "Character/VBTargetLockComponent.h"   // 가드 진입 자동 락온
#include "AbilitySystemComponent.h"
#include "Data/VBWeaponDefinition.h"   // 무기 게이트 - 이 스펙을 준 무기 정의
#include "VB/VB.h"

UVBGA_Guard::UVBGA_Guard()
{
	InputTag = VBGameplayTags::Input_Combat_Guard;
	bCancelOnInputRelease = true; // 누르고 있는 동안만 막는다 - 놓으면 끝
	// 가드의 입구는 서버 누름 수락 하나다(UVBCombatInputComponent) - 칼집 상태 누름이 서버에서 꺼낸 뒤 켜는 길과 같은 문.
	NetSecurityPolicy = EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution;

	// 이름표. 무기 교체(UVBWeaponStateComponent)가 이것으로 가드를 끊고, 전투 규칙 표(DA_CombatRules)가 이것으로 가드의 규칙을 찾는다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Guard));

	// 가드 상태 태그(피해 허브, 애님이 읽는다). 붙는 순간 가드가 끊는 공격이 취소된다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Guarding);
}

bool UVBGA_Guard::CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayTagContainer* SourceTags, const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	if (!Super::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags))
	{
		return false;
	}
	// 무기 세트가 준 가드만 켠다(SourceObject = 무기 정의). 가드 컴포넌트가 없는 액터는 막는다.
	// 켜기 전에 묻는다 - 켜진 뒤 거절하면 스윙만 끊기고 가드는 없다.
	const UAbilitySystemComponent* ASC = ActorInfo ? ActorInfo->AbilitySystemComponent.Get() : nullptr;
	const FGameplayAbilitySpec* Spec = ASC ? ASC->FindAbilitySpecFromHandle(Handle) : nullptr;
	// 방패 가드(Ability.Defense.Guard.Shield)가 패시브를 요구하는 것은 전투 규칙 표가 정한다 - 여기서 따로 보지 않는다.
	return Spec && Cast<UVBWeaponDefinition>(Spec->SourceObject.Get())
		&& UVBGuardComponent::FindFor(ActorInfo->AvatarActor.Get()) != nullptr;
}

void UVBGA_Guard::ExecuteAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 서버만 가드를 시작한다. 시작 못 하면 능력을 끝낸다 - 남으면 Guarding 태그가 공격만 막는다.
	UVBGuardComponent* Guard = UVBGuardComponent::FindFor(GetAvatarActorFromActorInfo());
	if (!Guard || (ActorInfo && ActorInfo->IsNetAuthority() && !Guard->BeginGuard()))
	{
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// 가드를 올리는 순간 자동 락온. 대상 탐색이 카메라 기반이라 로컬 조종자만 한다.
	// ToggleLockOn 은 이미 락온이면 해제하므로 쓰지 않는다.
	if (ActorInfo && ActorInfo->IsLocallyControlled())
	{
		// 플래그는 이 GA 가 소유한다 - 락온 컴포넌트 유무와 무관하게 읽힌다.
		if (bAutoLockOnGuard)
		{
			if (UVBTargetLockComponent* TLC = UVBTargetLockComponent::FindFor(GetAvatarActorFromActorInfo()))
			{
				TLC->TryLockOnFromLocalView();
			}
		}
	}
}

void UVBGA_Guard::EndAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
	const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled)
{
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (UVBGuardComponent* Guard = UVBGuardComponent::FindFor(GetAvatarActorFromActorInfo()))
		{
			Guard->EndGuard();
		}
	}

	Super::EndAbility(Handle, ActorInfo, ActivationInfo, bReplicateEndAbility, bWasCancelled);
}
