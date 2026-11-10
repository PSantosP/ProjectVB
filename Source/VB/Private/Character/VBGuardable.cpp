// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBGuardable.h"

#include "AbilitySystemBlueprintLibrary.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h" // GetAssetTags - RiposteWindowSeconds
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBParryConfig.h"
#include "GameplayEffect.h"
#include "UObject/ObjectKey.h"
#include "VB/VB.h"

namespace
{
	// 패링당한 공격자의 기절 게이지를 채운다. 공격 피해와 같은 계산 경로라 무효, 창, 회복 규칙이 그대로 걸린다.
	void ApplyParryPoise(UAbilitySystemComponent& ParrierASC, UAbilitySystemComponent& AttackerASC, const UVBParryConfig& Config)
	{
		if (Config.ParryPoiseDamage <= 0.0f)
		{
			return;
		}

		const FString Problem = UVBParryConfig::GetPoiseEffectProblem(Config.ParryPoiseEffect);
		if (!Problem.IsEmpty())
		{
			// 설정 사고다(IsDataValid 가 막는다). 패링마다 찍으면 다른 로그를 덮으므로 설정마다 한 번 크게 말한다.
			static TSet<FObjectKey> Reported;
			bool bAlreadyReported = false;
			Reported.Add(FObjectKey(&Config), &bAlreadyReported);
			VB_CLOG(!bAlreadyReported, Error, "패링 게이지: %s 의 ParryPoiseEffect 를 쓸 수 없다(%s) - 패링이 공격자의 기절 게이지를 채우지 못한다",
				*GetNameSafe(&Config), *Problem);
			return;
		}

		const FGameplayEffectSpecHandle Spec = ParrierASC.MakeOutgoingSpec(Config.ParryPoiseEffect, 1.0f, ParrierASC.MakeEffectContext());
		if (!Spec.IsValid())
		{
			VB_LOG(Error, "패링 게이지: %s 로 효과 스펙을 못 만들었다 - 공격자 %s 의 게이지를 채우지 못했다",
				*GetNameSafe(Config.ParryPoiseEffect.Get()), *GetNameSafe(AttackerASC.GetAvatarActor()));
			return;
		}

		Spec.Data->SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, Config.ParryPoiseDamage);
		ParrierASC.ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), &AttackerASC);
	}
}

float VBGuardRules::RiposteWindowSeconds(const UAbilitySystemComponent& ASC, const UVBParryConfig& Config)
{
	for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
	{
		if (Spec.Ability && Spec.Ability->GetAssetTags().HasTag(VBGameplayTags::Ability_Attack_Riposte))
		{
			return Config.RiposteWindow;
		}
	}

	return 0.0f;
}

FVBGuardResult IVBGuardable::ResolveIncomingHit(UAbilitySystemComponent& VictimASC, const FGameplayEffectSpec& Spec, float Damage)
{
	FVBGuardResult Result;
	Result.Damage = Damage;
	if (!IsGuarding())
	{
		return Result;
	}
	// 공격 스펙이 Attack.Answer.DodgeOnly 를 실었으면 가드, 패링을 건너뛴다 - 예고 큐의 빨강 약속을 여기서 지킨다.
	//  값의 홈은 UVBAttackConfig::AttackAnswer 하나다. 회피 무적은 그대로 통한다('회피만' 이지 '무적' 이 아니다).
	if (Spec.GetDynamicAssetTags().HasTagExact(VBGameplayTags::Attack_Answer_DodgeOnly))
	{
		VB_LOG(Log, "Unblockable : guard ignored (Attack.Answer.DodgeOnly)");
		return Result;
	}
	// GetParryConfig 는 미할당 시 CDO 를 돌려주므로 null 이 아니다(계약 1). 폴백 대신 ensure 로 계약을 못박고, 깨진 프레임엔 가드 판정을 건너뛴다 -
	//  튜닝값 없이 패링/블록을 흉내내면 값이 갈린다.
	const UVBParryConfig* PCfg = GetParryConfig();
	if (!ensureMsgf(PCfg, TEXT("IVBGuardable::GetParryConfig() returned null - CDO fallback contract violated")))
	{
		return Result;
	}

	// 블록, 패링 공통: 받아넘김(Accept) 연출 + 플린치 억제. 차이는 피해량과 공격자 경직뿐.
	Result.bGuarded = true;
	const FGameplayEffectContextHandle& HitContext = Spec.GetContext();
	const AActor* HitAttacker = HitContext.GetEffectCauser() ? HitContext.GetEffectCauser() : HitContext.GetInstigator();
	PlayGuardAccept(VBComputeHitDirection(VictimASC.GetAvatarActor(), HitAttacker));
	const bool bPerfectParry = GetGuardElapsed() <= PCfg->ParryWindow;
	Result.bPerfectParry = bPerfectParry;
	if (bPerfectParry)
	{
		// 패링: 피해 무효 + 스태미나 회복 + 반격 창 + 공격자 움찔 + 공격자 기절 게이지. 창 수명은 캐릭터가 관리한다.
		VB_LOG(Log, "Parry : Damage nullified + attacker stagger + attacker poise");
		Result.Damage = 0.0f;
		ApplyGuardStaminaDelta(+PCfg->StaminaGainOnParry);
		// causer 가 공격하는 액터다. instigator 는 PlayerState 일 수 있어 facing, lock 이 겨눌 수 없다.
		//   causer 없이 적용된 효과는 instigator 로 대신한다.
		const FGameplayEffectContextHandle& Context = Spec.GetContext();
		AActor* ParriedAttacker = Context.GetEffectCauser() ? Context.GetEffectCauser() : Context.GetInstigator();
		OpenRiposteWindow(VBGuardRules::RiposteWindowSeconds(VictimASC, *PCfg), ParriedAttacker);
		// 경직에 GE_Stagger 를 쓰지 않는다(no-op 이다). 공격자 히트리액트(Event.Combat.Hit)를 재사용한다.
		if (UAbilitySystemComponent* AttackerASC = UAbilitySystemBlueprintLibrary::GetAbilitySystemComponent(ParriedAttacker))
		{
			FGameplayEventData StaggerEvent;
			StaggerEvent.EventTag = VBGameplayTags::Event_Combat_Hit;
			StaggerEvent.Instigator = VictimASC.GetAvatarActor(); // 되받아치는 주체 = 패링한 쪽
			StaggerEvent.Target = ParriedAttacker;
			StaggerEvent.EventMagnitude = PCfg->StaggerHitReactMagnitude;
			AttackerASC->HandleGameplayEvent(VBGameplayTags::Event_Combat_Hit, &StaggerEvent);
			// 움찔 뒤에 채운다 - 이 패링으로 게이지가 다 차면 처형 창이 열리며 움찔을 끊고 대기 자세가 된다.
			ApplyParryPoise(VictimASC, *AttackerASC, *PCfg);
		}
	}
	else
	{
		// 블록: 피해 감쇠(창 밖 가드 유지 중). 공격자 경직 없음. 막을수록 스태미나가 준다 - 패링에는 비용이 없어
		//  '정확히 받아치면 손해가 없다' 는 대비가 손맛을 만든다.
		VB_LOG(Log, "Block : Damage x%.2f (Accept, no flinch)", PCfg->BlockDamageMultiplier);
		Result.Damage = Damage * PCfg->BlockDamageMultiplier;
		ApplyGuardStaminaDelta(-PCfg->StaminaCostOnBlock);
	}

	// 성공 판정 한 곳에서 방어 큐 하나만 쏜다(맞은 자리). 일반 가드는 약한 자산/슬로우 없는 자식을 쓴다.
	FGameplayCueParameters GuardCue;
	GuardCue.EffectContext = Spec.GetContext();
	GuardCue.Instigator = Spec.GetContext().GetInstigator();
	if (const FHitResult* ContactHit = GuardCue.EffectContext.GetHitResult())
	{
		GuardCue.Location = ContactHit->ImpactPoint;
		GuardCue.Normal = ContactHit->ImpactNormal;
	}

	VictimASC.ExecuteGameplayCue(
		bPerfectParry ? VBGameplayTags::GameplayCue_Combat_Parry : VBGameplayTags::GameplayCue_Combat_GuardBlock, GuardCue);
	return Result;
}
