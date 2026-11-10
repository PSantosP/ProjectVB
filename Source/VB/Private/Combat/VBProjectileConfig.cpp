// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Combat/VBProjectileConfig.h"

#include "GameplayEffect.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystem/VBHitSpec.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

UVBProjectileConfig::UVBProjectileConfig()
{
	// 던진 돌은 막을 수 없는 공격이 기본이다. 네이티브 태그는 헤더 초기화로 못 넣어 생성자에서 준다.
	AttackAnswer = VBGameplayTags::Attack_Answer_DodgeOnly;
}

FVBHitSpecValues UVBProjectileConfig::MakeHitValues(float AttackPower, float PushYaw) const
{
	FVBHitSpecValues Values;
	Values.Damage = AttackPower * DamageMultiplier;
	Values.KnockbackStrength = KnockbackStrength;
	Values.PoiseDamage = PoiseDamage;
	Values.HitImpact = HitImpact;
	Values.PushYaw = PushYaw;
	Values.ChargeSource = EVBCombatChargeSource::OrdinaryAttack;
	Values.AttackAnswer = AttackAnswer;
	// 피 이펙트는 싣지 않는다 - 닿은 자리 큐는 투사체가 직접 내고, 받는 쪽이 같은 자리에 하나 더 내면 겹친다.
	return Values;
}

#if WITH_EDITOR
#define LOCTEXT_NAMESPACE "VBProjectileConfig"

EDataValidationResult UVBProjectileConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Fail = [&Context, &Result](const FText& Message)
	{
		Context.AddError(Message);
		Result = EDataValidationResult::Invalid;
	};
	if (!FMath::IsFinite(KnockbackZ) || !FMath::IsNearlyZero(KnockbackZ))
	{
		Fail(LOCTEXT("LegacyPop", "KnockbackZ는 폐기된 상방 밀치기 값입니다. 0으로 설정하세요."));
	}

	if (!DamageEffectClass)
	{
		Fail(LOCTEXT("NoEffect", "DamageEffectClass 가 비었습니다 - 맞혀도 피해가 없습니다."));
	}

	if (Speed <= 0.0f)
	{
		Fail(LOCTEXT("NoSpeed", "Speed 는 0 보다 커야 합니다 - 투사체가 날아가지 않습니다."));
	}

	if (LifetimeSeconds <= 0.0f)
	{
		Fail(LOCTEXT("NoLifetime", "LifetimeSeconds 는 0 보다 커야 합니다 - 빗나간 투사체가 사라지지 않습니다."));
	}

	if (CollisionRadius <= 0.0f)
	{
		Fail(LOCTEXT("NoRadius", "CollisionRadius 는 0 보다 커야 합니다 - 아무것에도 닿지 않습니다."));
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
