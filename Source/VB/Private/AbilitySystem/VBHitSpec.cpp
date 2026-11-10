// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/VBHitSpec.h"

#include "GameplayEffect.h"
#include "Engine/HitResult.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "VB/VB.h"

bool VBHitSpec::Fill(FGameplayEffectSpec& Spec, const FVBHitSpecValues& Values, const FHitResult& Hit)
{
	FGameplayEffectContextHandle ContextHandle = Spec.GetContext();
	FVBGameplayEffectContext* const HitFacts = FVBGameplayEffectContext::Get(ContextHandle);
	if (!HitFacts)
	{
		VB_LOG(Error, "타격 스펙: 효과 컨텍스트가 FVBGameplayEffectContext 가 아니다(%s) - Globals 설정을 확인하라. %s 에 피해를 주지 않는다",
			ContextHandle.Get() ? *ContextHandle.Get()->GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(Hit.GetActor()));
		return false;
	}
	// 엔진은 두 번째 HitResult 를 assert 로 막는다 - 스펙은 대상마다 새로 만들어야 한다.
	if (ContextHandle.GetHitResult())
	{
		VB_LOG(Error, "타격 스펙: 컨텍스트에 이미 HitResult 가 있다 - 대상마다 새 스펙이 필요하다. %s 에 피해를 주지 않는다",
			*GetNameSafe(Hit.GetActor()));
		return false;
	}

	ContextHandle.AddHitResult(Hit);
	VB_CLOG(FVector(Hit.ImpactPoint).IsNearlyZero(), Warning, "타격 스펙: %s 에 닿은 점이 없다 - 맞은 번쩍임이 나오지 않는다",
		*GetNameSafe(Hit.GetActor()));

	HitFacts->SetPushYaw(Values.PushYaw);
	HitFacts->SetChargeSource(Values.ChargeSource);
	HitFacts->SetDeclaredHitImpact(Values.HitImpact);
	HitFacts->SetLaunchHeight(Values.LaunchHeight);
	HitFacts->SetImpactCue(Values.ImpactCue);

	Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, Values.Damage);
	Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackStrength, Values.KnockbackStrength);
	Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, Values.PoiseDamage);
	// 받는 쪽 허브가 DodgeOnly 를 읽어 가드, 패링을 건너뛴다 - 예고 큐가 보여 준 답과 같은 값이다.
	if (Values.AttackAnswer.IsValid())
	{
		Spec.AddDynamicAssetTag(Values.AttackAnswer);
	}

	return true;
}
