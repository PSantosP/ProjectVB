// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "VBGA_Riposte.generated.h"

/**
 * 리포스트 - 패링 성공 직후 State.Combat.RiposteWindow 동안 열리는 반격.
 * 경공격과 같은 입력을 쓰고 어느 쪽이 뜰지는 태그가 정한다(입력 분배가 실패 시 다음 후보를 시도한다).
 * 무기 세트가 경공격과 함께 주며, 몽타주는 무기 정의의 반격 잎이다.
 */
UCLASS()
class VB_API UVBGA_Riposte : public UVBGA_MeleeAttackBase
{
	GENERATED_BODY()

public:
	UVBGA_Riposte();
	virtual UVBAttackConfig* SelectWeaponLeaf(const UVBWeaponDefinition& Weapon) const override;
};
