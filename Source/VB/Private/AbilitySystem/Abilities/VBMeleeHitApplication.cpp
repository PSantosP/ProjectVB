// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBMeleeHitApplication.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBSwingHitContext.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBSwingCombatProfile.h"
#include "Engine/HitResult.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "VB/VB.h"

UAbilitySystemComponent* VBMeleeHitApplication::FindDamageableTarget(const FHitResult& Hit, const ACharacter& Avatar,
	const TSet<AActor*>& ProcessedActors)
{
	AActor* HitActor = Hit.GetActor();
	if (!HitActor || ProcessedActors.Contains(HitActor))
	{
		return nullptr;
	}

	if (UVBAbilitySystemStatics::IsFriendlyFire(&Avatar, HitActor))
	{
		return nullptr;
	}

	UAbilitySystemComponent* TargetASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(HitActor);
	if (!TargetASC || UVBAbilitySystemStatics::IsDead(TargetASC) || !Hit.bBlockingHit)
	{
		return nullptr;
	}

	return TargetASC;
}

FVBGameplayEffectContext* VBMeleeHitApplication::AddHitToSpec(const FGameplayEffectSpecHandle& SpecHandle, const FHitResult& Hit,
	const FVBSwingHitContext& Context, const UObject& Ability)
{
	AActor* HitActor = Hit.GetActor();

	// HitResult 를 GE Context 에 넣어야 GCN 이 피격자를 역추출할 수 있다.
	FGameplayEffectContextHandle ContextHandle = SpecHandle.Data.Get()->GetContext();
	ContextHandle.AddHitResult(Hit);
	VB_CLOG(FVector(Hit.ImpactPoint).IsNearlyZero(), Warning, "[Swing] %s hit %s with no impact point - it draws no hit flash",
		*Ability.GetName(), *GetNameSafe(HitActor));

	// 타격 사실은 같은 컨텍스트에 싣는다. 우리 타입이 아니면 이 대상을 건너뛴다(Globals 설정 누락).
	FVBGameplayEffectContext* const HitFacts = FVBGameplayEffectContext::Get(ContextHandle);
	if (!HitFacts)
	{
		VB_LOG(Error, "근접 공격: 효과 컨텍스트가 FVBGameplayEffectContext 가 아니다(%s) - Globals 설정을 확인하라. %s 에 피해를 주지 않는다",
			ContextHandle.Get() ? *ContextHandle.Get()->GetScriptStruct()->GetName() : TEXT("null"), *GetNameSafe(HitActor));
		return nullptr;
	}

	// 피격자는 공격자의 조준 보정과 스윙 축을 볼 수 없어서, 미는 방향을 여기서 확정해 보낸다.
	HitFacts->SetPushYaw(Context.KnockbackYaw);
	return HitFacts;
}

void VBMeleeHitApplication::FillHitSpec(FGameplayEffectSpec& Spec, FVBGameplayEffectContext& HitFacts, const FHitResult& Hit,
	const FVBSwingHitContext& Context, const UVBAttackConfig& Config)
{
	// 반응 등급과 띄우는 높이. 받는 쪽이 저항을 빼서 실효 등급을 정한다.
	HitFacts.SetDeclaredHitImpact(Context.SwingImpact);
	HitFacts.SetLaunchHeight(Context.SwingLaunchHeight);

	// 피 이펙트는 여기서 고르기만 한다. 막았는지 피했는지는 맞은 쪽이 정하므로 실행은 VBDamageResponse 가 한다.
	// 한 창에 여러 부위가 있으면 닿은 부위의 큐를, 스윙이 강타면 그 큐의 강타판을 쓴다.
	const FVBSwingHitContext::FPart* Part = Context.Parts.FindByPredicate(
		[&Hit](const FVBSwingHitContext::FPart& Entry) { return Entry.Bone == Hit.MyBoneName; });
	const FGameplayTag PickedCue = (Part && Part->Cue.IsValid()) ? Part->Cue : Context.ImpactCueTag;
	HitFacts.SetImpactCue(UVBSwingCombatProfile::ResolveImpactCue(Context.CombatProfile.Get(),
		PickedCue.IsValid() ? PickedCue : Config.GameplayCueTag));
	HitFacts.SetStrikingShape(Part ? Part->Shape : Context.StrikeShape);

	// 피해를 나르는 스펙에 넉백 세기와 경직 피해를 함께 싣는다. 스윙 배율은 잎 값을 연타에 나눌 뿐이다.
	Spec.SetSetByCallerMagnitude(VBGameplayTags::Effect_Damage_Physical, Context.FinalDamage);
	Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_KnockbackStrength,
		Config.KnockbackStrength * Context.SwingScales.KnockbackStrengthScale);
	Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, Config.PoiseDamage * Context.SwingScales.PoiseDamageScale);

	// 답 태그. 피격자 허브가 DodgeOnly 를 읽어 가드, 패링을 건너뛴다(예고 큐와 같은 자산 값).
	if (Config.AttackAnswer.IsValid())
	{
		Spec.AddDynamicAssetTag(Config.AttackAnswer);
	}
}

bool VBMeleeHitApplication::ApplyHitSpec(UAbilitySystemComponent& TargetASC, const FGameplayEffectSpecHandle& SpecHandle)
{
	// 겹침, 무적, 완전 방어만으로 카메라가 돌지 않게 실제 체력 또는 실드 감소를 확인한다.
	const float HealthBefore = TargetASC.GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	const float ShieldBefore = TargetASC.GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute());
	TargetASC.ApplyGameplayEffectSpecToSelf(*SpecHandle.Data.Get());
	return TargetASC.GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) < HealthBefore
		|| TargetASC.GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute()) < ShieldBefore;
}

void VBMeleeHitApplication::LogSwingDamage(const UObject& Ability, const FVBSwingHitContext& Context, const UVBAttackConfig& Config,
	float VariantDamage, int32 TargetCount)
{
	// 곱셈 사슬 전체를 남긴다 - 최종 피해의 유일한 관측 지점(적중한 스윙만).
	VB_LOG(Log, "%s: 데미지 %.1f = AttackPower %.1f x cfg %.2f x variant %.2f x swing %.2f x enhance %.2f(%s) (대상 %d) / 선언 등급 %s",
		*Ability.GetName(), Context.FinalDamage, Context.AttackPower, Config.DamageMultiplier,
		VariantDamage, Context.SwingScales.DamageScale, Context.WeaponEnhanceScale,
		*StaticEnum<EVBWeaponType>()->GetNameStringByValue((int64)Context.WeaponType), TargetCount, VBHitImpactName(Context.SwingImpact));

	// 저작한 스윙에서만 1줄. 미저작이 정상 상태이므로 매 스윙 로그는 노이즈다.
	// 꽂았는데 이 줄이 안 뜨면 배선이 안 된 것 - 그것이 이 로그의 유일한 목적이다.
	if (const UVBSwingCombatProfile* CombatProfile = Context.CombatProfile.Get())
	{
		VB_LOG(Log, "%s: 스윙 배율 적용 profile=%s dmg=%.2f kbS=%.2f hitstop=%.2f poise=%.2f impact=%s",
			*Ability.GetName(), *CombatProfile->GetName(),
			Context.SwingScales.DamageScale, Context.SwingScales.KnockbackStrengthScale,
			Context.SwingScales.HitStopDurationScale, Context.SwingScales.PoiseDamageScale,
			CombatProfile->bOverrideHitImpact ? VBHitImpactName(CombatProfile->HitImpact) : TEXT("상속"));
	}
}
