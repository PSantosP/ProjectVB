// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBGA_MeleeAttackBase.h"
#include "VBGA_LightAttack.generated.h"

/**
 * 약공격 Ability
 * CommitAbility -> 무기 판정 창(노티) -> GE_Damage 적용. SphereTrace 는 창이 없을 때의 즉시 판정 되돌림 길이다
 * BP 서브클래스에서 DamageEffect, AttackRange 등 설정
 */
UCLASS(Abstract)
class VB_API UVBGA_LightAttack : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()
	
public:
	UVBGA_LightAttack();
	virtual UVBAttackConfig* SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const override;
};