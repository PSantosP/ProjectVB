// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBGA_CinematicSkill.h"
#include "VBInputDispatchFixtureAbilities.h" // UVBInputFixtureAbility - 누가 끝낼 때까지 켜져 있다
#include "VBCinematicSkillTestTypes.generated.h"

// VB.Unit.CinematicSkill.BlocksOrdinaryActions 용 - 정의, Montage 없이 실제 스킬 생성자(태그)만 쓴다.
//   시험이 끝낼 때까지 켜져 있어서 도는 동안 무엇을 막는지 물을 수 있다.
UCLASS(NotBlueprintable)
class UVBCinematicSkillTestShow : public UVBGA_CinematicSkill
{
	GENERATED_BODY()

public:
	virtual bool CanActivateAbility(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayTagContainer* SourceTags = nullptr, const FGameplayTagContainer* TargetTags = nullptr,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override
	{
		// 태그 관문만 본다 - 스킬과 근접 기반 클래스의 정의, 준비 검사는 이 시험이 묻는 것이 아니다.
		return UGameplayAbility::CanActivateAbility(Handle, ActorInfo, SourceTags, TargetTags, OptionalRelevantTags);
	}
	// 행동 막기를 게이지, 저작된 cost 와 떼어 본다.
	virtual bool CheckCost(const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		FGameplayTagContainer* OptionalRelevantTags = nullptr) const override
	{
		return UGameplayAbility::CheckCost(Handle, ActorInfo, OptionalRelevantTags);
	}

protected:
	virtual void ExecuteAbility(const FGameplayAbilitySpecHandle, const FGameplayAbilityActorInfo*,
		const FGameplayAbilityActivationInfo, const FGameplayEventData*) override {}
};

// 막기가 맞춰 보는 asset tag 로 만든 보통 행동들. 실제 guard 는 플레이어 몸이 아니면 거절해서 대역을 두고,
//   그 태그가 실제 BP_GA_Guard 와 같은지는 시험이 본다.
UCLASS(NotBlueprintable)
class UVBCinematicSkillTestGuard : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBCinematicSkillTestGuard() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Guard)); }
};

UCLASS(NotBlueprintable)
class UVBCinematicSkillTestLight : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBCinematicSkillTestLight() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Attack_Light)); }
};

UCLASS(NotBlueprintable)
class UVBCinematicSkillTestDodge : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBCinematicSkillTestDodge() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Defense_Dodge)); }
};

UCLASS(NotBlueprintable)
class UVBCinematicSkillTestInteract : public UVBInputFixtureAbility
{
	GENERATED_BODY()
public:
	UVBCinematicSkillTestInteract() { SetAssetTags(FGameplayTagContainer(VBGameplayTags::Ability_Interact)); }
};
