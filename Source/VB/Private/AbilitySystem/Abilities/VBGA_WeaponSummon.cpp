// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_WeaponSummon.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "VB/VB.h"

UVBGA_WeaponSummon::UVBGA_WeaponSummon()
{
	bIsToggleAbility = false;
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Weapon_Summon));
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Weapon_Summoning);

	InputTag = VBGameplayTags::Input_WeaponSummon;
}

void UVBGA_WeaponSummon::ExecuteAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayAbilityActivationInfo ActivationInfo,
	const FGameplayEventData* TriggerEventData)
{
	Super::ExecuteAbility(Handle, ActorInfo, ActivationInfo, TriggerEventData);

	// 소환은 플레이어 캐릭터만 받는다. 무기 상태가 없으면 캐릭터 조립이 깨진 것이다.
	UVBWeaponStateComponent* WSC = GetWeaponStateFromActorInfo();
	if (!WSC)
	{
		VB_LOG(Error, "%s: 아바타 %s 에 무기 상태 컴포넌트가 없다 - 소환을 끝낸다", *GetName(), *GetNameSafe(GetAvatarActorFromActorInfo()));
		EndAbility(Handle, ActorInfo, ActivationInfo, true, true);
		return;
	}

	// Summon/Sheathe 는 내부에서 서버 RPC 로 간다. 클라까지 부르면 불필요한 RPC 와 중복 호출이 생긴다.
	if (ActorInfo && ActorInfo->IsNetAuthority())
	{
		if (WSC->IsWeaponSummoned())
		{
			WSC->SheatheWeapon();
		}
		else
		{
			// 선택 무기 -> 마지막 사용 무기 -> 기본 무기 순. 자동 납도 뒤에도 직전 무기를 다시 꺼낸다.
			WSC->SummonWeapon(WSC->GetResummonWeaponType());
		}
	}

	EndAbility(Handle, ActorInfo, ActivationInfo, true, false);
}
