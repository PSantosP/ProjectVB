// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Abilities/VBGA_Riposte.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBWeaponDefinition.h"

UVBGA_Riposte::UVBGA_Riposte()
{
	// 경공격과 같은 입력을 쓴다. 경합이 없는 근거는 헤더 주석 참조
	// (경공격은 Guarding 차단, 반격 창은 가드 중에만 열린다).
	InputTag = VBGameplayTags::Input_Attack_Light;

	// 방향 전환을 받지 않는다 - 패링이 공격자에게 락을 넘겼고, 반격은 그 공격자에게 간다.
	bAllowDirectionalLockSwitch = false;

	// 공격 상태는 공유한다(질주, 상호작용, 무기 꺼내기, 웅크리기가 이것으로 막힌다). 반격 창 요구와 가드의 끊기는 전투 규칙 표에 있다.
	ActivationOwnedTags.AddTag(VBGameplayTags::State_Combat_Attacking);

	// 이 GA 의 이름표. Ability.Attack 아래 잎이라 표의 "공격 전부" 막기에 걸린다. 경공격과 나눈 것은 반격만의 규칙(창 요구)을 쓰기 위해서다.
	SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Riposte));
}

UVBAttackConfig* UVBGA_Riposte::SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const
{
	return Weapon.GetRiposte();
}
