// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"

#include "AbilitySystemComponent.h"
#include "Engine/World.h"
#include "GameplayEffectExtension.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "VB/VB.h"

namespace
{
	// 슈퍼아머 중에는 파탄을 미룬다(Poise 0 유지, 이벤트 없음). 걷힌 뒤 첫 타에 경직이 터진다.
	// Stagger 의 ActivationBlockedTags 로 막으면 리필이 먼저 돌아 파탄이 삼켜진다.
	// 예외: 모든 아머 출처가 부서지는 아머면 게이지를 비운 타격에 바로 무너진다(보호된 출처가 있으면 보호가 이긴다).
	bool IsPoiseBreakHeldBySuperArmor(const UAbilitySystemComponent& OwnerASC)
	{
		if (!OwnerASC.HasMatchingGameplayTag(VBGameplayTags::State_Combat_SuperArmor))
		{
			return false;
		}

		if (OwnerASC.GetTagCount(VBGameplayTags::State_Combat_SuperArmor) !=
			OwnerASC.GetTagCount(VBGameplayTags::State_Combat_SuperArmor_Breakable))
		{
			VB_LOG(Log, "Poise 파탄 보류: %s 슈퍼아머 중 - 슈퍼아머가 걷힌 뒤 첫 타에 경직",
				*GetNameSafe(OwnerASC.GetAvatarActor()));
			return true;
		}

		VB_LOG(Log, "Poise armor break: %s breakable armor broken", *GetNameSafe(OwnerASC.GetAvatarActor()));
		return false;
	}
}

UVBCombatAttributeSet::UVBCombatAttributeSet()
{
	InitCinematicSkillGauge(0.0f);
	InitMaxCinematicSkillGauge(0.0f);
	InitStamina(0.0f);
	InitMaxStamina(0.0f);
	InitAttackPower(0.0f);
	// Poise 기본값 0 = 경직 시스템 미사용. 쓰는 액터는 GE_InitStats 가 MaxPoise 를 채운다(플레이어는 0 으로 남는다).
	InitPoise(0.0f);
	InitMaxPoise(0.0f);
	// 0 = 회복 없음. 저작하지 않은 액터(플레이어 포함)는 지금처럼 회복하지 않는다.
	InitPoiseRecoveryDelaySeconds(0.0f);
	InitPoiseRecoveryPerSecond(0.0f);
	InitIncomingPoiseDamage(0.0f);
	InitBaseDamage(0.0f);
	// 1.0 = 배수 없음. 다운 취약 GE 가 잠시 올리고, 끝나면 GE 제거로 돌아온다.
	InitDamageTakenScale(1.0f);
	InitWeaponDamageScale(1.0f);
	// 0 = 등급 그대로, 1.0 = 넉백 그대로. 저작 전 모든 액터의 현행을 지킨다.
	InitImpactResistance(0.0f);
	InitKnockbackTakenScale(1.0f);
}

void UVBCombatAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	if (ClampCinematicGauge(Attribute, NewValue))
	{
		return;
	}

	if (Attribute == GetStaminaAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxStamina());
	}
	else if (Attribute == GetMaxStaminaAttribute())
	{
		CachedMaxStamina = GetMaxStamina();
		NewValue = FMath::Max(1.0f, NewValue);
	}
	else if (Attribute == GetAttackPowerAttribute())
	{
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetPoiseAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxPoise());
	}
	else if (Attribute == GetImpactResistanceAttribute() || Attribute == GetKnockbackTakenScaleAttribute()
		|| Attribute == GetPoiseRecoveryDelaySecondsAttribute() || Attribute == GetPoiseRecoveryPerSecondAttribute())
	{
		// 음수 저항, 넉백 배수, 회복 지연, 속도는 뜻이 없어 0 에서 자른다. 위는 열어 둔다.
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetDamageTakenScaleAttribute() || Attribute == GetWeaponDamageScaleAttribute())
	{
		// 음수 배수는 피해를 회복으로 뒤집는다 - 0 아래는 자르고, 위는 열어 둔다(배수 크기는 GE 자산이 정한다).
		NewValue = FMath::Max(0.0f, NewValue);
	}
	else if (Attribute == GetMaxPoiseAttribute())
	{
		// 1 로 올려 치지 않는다 - 0 은 "경직 미사용" 이라는 유효한 값이다.
		NewValue = FMath::Max(0.0f, NewValue);
	}
}

void UVBCombatAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, CinematicSkillGauge, COND_OwnerOnly, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, MaxCinematicSkillGauge, COND_OwnerOnly, REPNOTIFY_Always);

	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, Stamina,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, MaxStamina,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, AttackPower,
		COND_None, REPNOTIFY_Always);
	// IncomingPoiseDamage 는 meta attribute 라 복제하지 않는다 - 서버에서 계산되고 즉시 0 으로 비워진다.
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, Poise,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBCombatAttributeSet, MaxPoise,
		COND_None, REPNOTIFY_Always);
}

bool UVBCombatAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}

	if ((Data.EvaluatedData.Attribute == GetCinematicSkillGaugeAttribute()
		|| Data.EvaluatedData.Attribute == GetMaxCinematicSkillGaugeAttribute())
		&& !FMath::IsFinite(Data.EvaluatedData.Magnitude))
	{
		VB_LOG(Error, "Non-finite cinematic gauge modifier rejected");
		return false;
	}
	// 체력의 무효 판정에 창이 켜지면 게이지를 다 찬 채로 두는 규칙을 더한 판정(UVBAbilitySystemStatics::ShouldIgnorePoiseHit).
	const TCHAR* Reason = nullptr;
	if (Data.EvaluatedData.Attribute == GetIncomingPoiseDamageAttribute() && Data.EvaluatedData.Magnitude > 0.0f
		&& UVBAbilitySystemStatics::ShouldIgnorePoiseHit(GetOwningAbilitySystemComponent(), Data.EffectSpec, &Reason))
	{
		VB_LOG(Log, "%s: %s 의 기절 게이지 피해 %.1f 을 받지 않는다", Reason, *GetNameSafe(GetAvatarActorFromOwner()), Data.EvaluatedData.Magnitude);
		return false;
	}

	return true;
}

void UVBCombatAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);

	// 저항, 넉백 배수, 피해 배수는 기본값도 0 에서 자른다. 즉시 GE 는 기본값에 쓰기 때문이다.
	// 현재값으로 기본값을 덮으면 지속 GE 의 몫이 두 번 들어가므로 기본값을 읽어 고친다.
	if (Data.EvaluatedData.Attribute == GetImpactResistanceAttribute() || Data.EvaluatedData.Attribute == GetKnockbackTakenScaleAttribute()
		|| Data.EvaluatedData.Attribute == GetDamageTakenScaleAttribute())
	{
		if (UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent())
		{
			if (OwnerASC->GetNumericAttributeBase(Data.EvaluatedData.Attribute) < 0.0f)
			{
				OwnerASC->SetNumericAttributeBase(Data.EvaluatedData.Attribute, 0.0f);
			}
		}

		return;
	}

	if (Data.EvaluatedData.Attribute == GetStaminaAttribute())
	{
		SetStamina(FMath::Clamp(GetStamina(), 0.0f, GetMaxStamina()));
	}
	else if (Data.EvaluatedData.Attribute == GetMaxStaminaAttribute())
	{
		if (CachedMaxStamina > 0.0f)
		{
			const float Ratio = GetStamina() / CachedMaxStamina;
			SetStamina(FMath::Clamp(Ratio * GetMaxStamina(), 0.0f, GetMaxStamina()));
		}
	}
	else if (Data.EvaluatedData.Attribute == GetAttackPowerAttribute())
	{
		SetAttackPower(FMath::Max(0.0f, GetAttackPower()));
	}
	else if (Data.EvaluatedData.Attribute == GetIncomingPoiseDamageAttribute())
	{
		ProcessIncomingPoiseDamage(Data);
	}
}

void UVBCombatAttributeSet::ProcessIncomingPoiseDamage(const FGameplayEffectModCallbackData& Data)
{
	// 메타 속성 파이프라인. IncomingDamage(UVBHealthAttributeSet)와 같은 형태다.
	const float PoiseDamageValue = GetIncomingPoiseDamage();
	SetIncomingPoiseDamage(0.0f); // 다음 계산을 위해 즉시 비운다

	if (PoiseDamageValue <= 0.0f)
	{
		return;
	}

	UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
	if (!OwnerASC || !OwnerASC->IsOwnerActorAuthoritative())
	{
		return; // 경직 판정은 서버 권위 전용
	}

	if (UVBAbilitySystemStatics::IsDead(OwnerASC))
	{
		// 시체는 비틀거리지 않는다. GE_PhysicalDamage 의 모디파이어 순서가 이 게이트를 성립시킨다
		// (IncomingDamage 가 먼저 돌아 State.Dead 가 붙은 뒤 여기 온다).
		return;
	}

	if (GetMaxPoise() <= 0.0f)
	{
		// MaxPoise <= 0 = 경직 미사용. 이 게이트가 없으면 MaxPoise 가 없는 플레이어가 매 피격마다 경직된다.
		return;
	}

	SetPoise(FMath::Max(0.0f, GetPoise() - PoiseDamageValue));
	NotePoiseDamage();
	if (GetPoise() > 0.0f)
	{
		return;
	}

	if (IsPoiseBreakHeldBySuperArmor(*OwnerASC))
	{
		return;
	}

	BreakPoise(*OwnerASC, Data);
}

void UVBCombatAttributeSet::BreakPoise(UAbilitySystemComponent& OwnerASC, const FGameplayEffectModCallbackData& Data)
{
	// 즉시 리필. 리필하지 않으면 0 인 상태가 유지되어 다음 타격마다 연속으로 경직이 터진다.
	SetPoise(GetMaxPoise());

	FGameplayEventData PoiseBrokenEvent;
	PoiseBrokenEvent.EventTag = VBGameplayTags::Event_Combat_PoiseBroken;
	PoiseBrokenEvent.Instigator = Data.EffectSpec.GetContext().GetInstigator();
	PoiseBrokenEvent.Target = OwnerASC.GetAvatarActor();
	PoiseBrokenEvent.ContextHandle = Data.EffectSpec.GetContext();
	// 이 사건으로 켜진 능력 수. 0 은 흔하다(경직 중, 쿨다운, 규칙 차단) - 리필만 됐다는 기록이다.
	if (OwnerASC.HandleGameplayEvent(VBGameplayTags::Event_Combat_PoiseBroken, &PoiseBrokenEvent) == 0)
	{
		VB_LOG(Log, "기절 게이지 파탄: %s - 이번 파탄으로 켜진 능력 없음(경직 중 · 쿨다운 · 차단이면 정상), 게이지만 다시 찼다", *GetNameSafe(OwnerASC.GetAvatarActor()));
	}
}

void UVBCombatAttributeSet::OnRep_Stamina(const FGameplayAttributeData& OldStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, Stamina, OldStamina);
}

void UVBCombatAttributeSet::OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, MaxStamina, OldMaxStamina);
}

void UVBCombatAttributeSet::OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, AttackPower, OldAttackPower);
}

void UVBCombatAttributeSet::OnRep_Poise(const FGameplayAttributeData& OldPoise)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, Poise, OldPoise);
}

void UVBCombatAttributeSet::OnRep_MaxPoise(const FGameplayAttributeData& OldMaxPoise)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, MaxPoise, OldMaxPoise);
}


void UVBCombatAttributeSet::NotePoiseDamage()
{
	// 회복 틱과 같은 월드(이 세트의 바깥 액터)에 건다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	LastPoiseDamageTime = World->GetTimeSeconds();
	if (GetPoiseRecoveryPerSecond() <= 0.0f || World->GetTimerManager().IsTimerActive(PoiseRecoveryTimer))
	{
		return;
	}
	// 틱 컴포넌트를 새로 두지 않는다 - 게이지가 덜 찬 동안만 도는 월드 타이머 하나다. 간격은 튜닝값이 아니라 구현 세부다.
	constexpr float RecoveryIntervalSeconds = 0.1f;
	World->GetTimerManager().SetTimer(PoiseRecoveryTimer,
		FTimerDelegate::CreateUObject(this, &UVBCombatAttributeSet::TickPoiseRecovery), RecoveryIntervalSeconds, /*InbLoop=*/true);
}

void UVBCombatAttributeSet::TickPoiseRecovery()
{
	// 타이머를 건 월드(이 세트의 바깥 액터의 월드)다. 월드가 없으면 타이머도 없다.
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}
	// 멈출 이유가 하나라도 있으면 타이머를 끄고 나간다 - 다음 게이지 피해가 다시 켠다.
	UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
	const float Rate = GetPoiseRecoveryPerSecond();
	if (!OwnerASC || !OwnerASC->IsOwnerActorAuthoritative() || UVBAbilitySystemStatics::IsDead(OwnerASC) || Rate <= 0.0f
		|| GetPoise() >= GetMaxPoise())
	{
		World->GetTimerManager().ClearTimer(PoiseRecoveryTimer);
		return;
	}
	// 이번 틱만 건너뛴다(창이 켜졌거나 지연 전). 건너뛴 시간은 회복에 넣지 않는다.
	const double Now = World->GetTimeSeconds();
	const double RecoveryStart = LastPoiseDamageTime + GetPoiseRecoveryDelaySeconds();
	if (UVBAbilitySystemStatics::IsFinisherWindowArmed(OwnerASC) || Now < RecoveryStart)
	{
		LastPoiseRecoveryTime = Now;
		return;
	}
	// 타이머 간격이 아니라 실제로 흐른 시간만큼 - 프레임이 늦어 타이머가 몰려 불려도 같은 양이 돌아온다.
	const double Elapsed = Now - FMath::Max(LastPoiseRecoveryTime, RecoveryStart);
	LastPoiseRecoveryTime = Now;
	SetPoise(FMath::Min(GetMaxPoise(), GetPoise() + Rate * static_cast<float>(Elapsed)));
}

bool UVBCombatAttributeSet::ClampCinematicGauge(const FGameplayAttribute& Attribute, float& Value) const
{
	if (Attribute != GetCinematicSkillGaugeAttribute() && Attribute != GetMaxCinematicSkillGaugeAttribute())
	{
		return false;
	}

	if (!FMath::IsFinite(Value))
	{
		VB_LOG(Error, "Non-finite cinematic gauge value rejected");
		Value = 0.0f;
	}

	Value = Attribute == GetMaxCinematicSkillGaugeAttribute() ? FMath::Max(0.0f, Value)
		: FMath::Clamp(Value, 0.0f, FMath::Max(0.0f, GetMaxCinematicSkillGauge()));
	return true;
}

void UVBCombatAttributeSet::PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const
{
	Super::PreAttributeBaseChange(Attribute, NewValue);
	ClampCinematicGauge(Attribute, NewValue);
}

void UVBCombatAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);
	if (Attribute == GetMaxCinematicSkillGaugeAttribute())
	{
		UAbilitySystemComponent* ASC = GetOwningAbilitySystemComponent();
		if (ASC && ASC->IsOwnerActorAuthoritative() && GetCinematicSkillGauge() > NewValue)
		{
			SetCinematicSkillGauge(FMath::Max(0.0f, NewValue));
		}
	}
}

void UVBCombatAttributeSet::OnRep_CinematicSkillGauge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, CinematicSkillGauge, OldValue);
}

void UVBCombatAttributeSet::OnRep_MaxCinematicSkillGauge(const FGameplayAttributeData& OldValue)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBCombatAttributeSet, MaxCinematicSkillGauge, OldValue);
}
