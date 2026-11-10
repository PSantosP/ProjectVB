// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBWeaponDefinition.h"

#include "AbilitySystem/Abilities/VBGA_AirCombo.h"
#include "AbilitySystem/Abilities/VBGA_Guard.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "AbilitySystem/VBAbilitySet.h"
#include "Animation/AnimMontage.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBWeaponDefinition"

TArray<FText> UVBWeaponDefinition::FindProblems() const
{
	TArray<FText> Problems;
	if (WeaponType == EVBWeaponType::None)
	{
		Problems.Add(LOCTEXT("NoType", "WeaponType 이 None - 맨손은 무기 정의가 없는 상태다."));
	}

	if (DisplayName.IsEmpty())
	{
		Problems.Add(LOCTEXT("NoDisplayName", "DisplayName 이 비었다 - 무기 휠에 이름 없이 나온다."));
	}

	if (!AbilitySet)
	{
		Problems.Add(LOCTEXT("NoAbilitySet", "능력 세트가 없다 - 이 무기를 들어도 공격 · 가드 · 장착 태그가 없다."));
		return Problems;
	}

	bool bGuards = false;
	bool bGrantsAirCombo = false;
	for (const FVBAbilitySetAbility& Entry : AbilitySet->GetGrantedAbilities())
	{
		bGuards |= Entry.Ability && Entry.Ability->IsChildOf<UVBGA_Guard>();
		bGrantsAirCombo |= Entry.Ability && Entry.Ability->IsChildOf<UVBGA_AirCombo>();
		// 잎이 비면 그 공격은 GA 자신의 설정으로 내려간다 - 무기의 공격이 아닌 것이 조용히 나가지 않게 저장 때 잡는다.
		const UVBGA_MeleeAttackBase* Attack = Entry.Ability ? Cast<UVBGA_MeleeAttackBase>(Entry.Ability->GetDefaultObject()) : nullptr;
		if (Attack && !Attack->SelectWeaponLeaf(*this))
		{
			Problems.Add(FText::Format(LOCTEXT("MissingLeaf", "세트가 주는 공격 '{0}' 의 잎이 이 무기에 없다."),
				FText::FromString(Entry.Ability->GetName())));
		}
	}

	if (AirCombo && !bGrantsAirCombo)
	{
		Problems.Add(LOCTEXT("MissingAirGrant", "AirCombo 설정이 있지만 능력 세트가 공중 콤보 능력을 지급하지 않는다."));
	}

	if (!bGuards || !GuardSet.GuardMontage)
	{
		return Problems;
	}
	// Montage_SetNextSection 은 없는 섹션 이름에도 성공으로 돌아와 가드 루프, 복귀가 조용히 깨진다 - 저장 때 잡는다.
	for (const FName Section : {GuardSet.GuardStartSection, GuardSet.GuardLoopSection, GuardSet.GuardEndSection})
	{
		if (GuardSet.GuardMontage->GetSectionIndex(Section) == INDEX_NONE)
		{
			Problems.Add(FText::Format(LOCTEXT("MissingGuardSection", "가드 몽타주 '{0}' 에 섹션 '{1}' 가 없다."),
				FText::FromString(GetNameSafe(GuardSet.GuardMontage)), FText::FromName(Section)));
		}
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBWeaponDefinition::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
