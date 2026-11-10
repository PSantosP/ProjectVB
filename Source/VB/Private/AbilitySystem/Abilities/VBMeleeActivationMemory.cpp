// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBMeleeActivationMemory.h"

#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"
#include "Data/VBAttackConfig.h"

int32 FVBMeleeActivationMemory::TakeVariant(int32 VariantCount)
{
	const int32 Chosen = VariantCursor;
	if (VariantCount > 1)
	{
		++VariantCursor;
	}

	return Chosen;
}

void FVBMeleeActivationMemory::NoteDodgeCut(int32 ResumeComboIndex, double Time, EVBWeaponType Weapon)
{
	DodgeComboIndex = ResumeComboIndex;
	DodgeTime = Time;
	DodgeWeapon = Weapon;
}

int32 FVBMeleeActivationMemory::PendingDodgeResume(const UVBAttackConfig* Config, EVBWeaponType Weapon,
	const UAnimMontage* Montage, double Now) const
{
	// 섹션은 이름 배열만으로 모자라고 끊긴 그 몽타주에 있어야 한다 - 한 구간짜리 질주 공격을 맞히고 회피하면 2타가 없다.
	const bool bPending = Config && DodgeComboIndex > 0 && Config->DodgeOffsetGraceSeconds > 0.0f
		&& (Now - DodgeTime) <= Config->DodgeOffsetGraceSeconds
		&& DodgeWeapon == Weapon
		&& DodgeComboIndex < Config->MaxComboCount
		&& Config->ComboSectionNames.IsValidIndex(DodgeComboIndex)
		&& VBMeleeAttackRules::HasComboSection(Montage, Config, DodgeComboIndex);
	return bPending ? DodgeComboIndex : INDEX_NONE;
}

void FVBMeleeActivationMemory::ConsumeDodgeResume()
{
	DodgeComboIndex = INDEX_NONE;
}
