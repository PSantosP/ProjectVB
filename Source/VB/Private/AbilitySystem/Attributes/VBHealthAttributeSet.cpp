// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "Net/UnrealNetwork.h"
#include "GameplayEffectExtension.h"
#include "GameFramework/Character.h"
#include "GameFramework/WorldSettings.h"
#include "GameModes/VBGameMode.h"
#include "Character/VBGuardable.h"          // 가드/패링 상태 조회(인터페이스 - 구체 클래스 의존 제거, FIND-057a)
#include "VB/VB.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBDamageResponse.h"           // 받아들인 피해의 반응(GAS G4)
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBHitReceivedMessage.h"
#include "CharacterCommon/VBCombatChargeCredit.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "HAL/IConsoleManager.h"
#include "Misc/ScopeExit.h"

#if !UE_BUILD_SHIPPING
// 개발 중 플레이어의 전투 피해 체력 하한만 변경한다.
static TAutoConsoleVariable<int32> CVarPlayerNoDeath(
	TEXT("vb.Combat.PlayerNoDeath"), 0,
	TEXT("Dev test: 1 = damage cannot take the player's Health below 1 (hits and reactions still happen). Enemies unaffected. 0 = off."),
	ECVF_Cheat);
#endif

namespace
{
	// 가드/패링 판정과 부수효과는 막는 쪽(IVBGuardable::ResolveIncomingHit)에 있고, 여기는 남은 피해와 "막았는가" 만 받는다.
	// 막았으면 뒤의 반응, 넉백, 섬광을 내지 않는다(가드는 받아넘김이지 피격이 아니다).
	// 피격자를 인터페이스로 보므로 적, 보스도 IVBGuardable 만 구현하면 이 길을 탄다.
	FVBGuardResult ResolveGuardAndAnnounceHit(UAbilitySystemComponent& OwnerASC, const FGameplayEffectSpec& Spec, float DamageValue)
	{
		FVBGuardResult Result;
		Result.Damage = DamageValue;
		if (IVBGuardable* Guardable = Cast<IVBGuardable>(OwnerASC.GetAvatarActor()))
		{
			Result = Guardable->ResolveIncomingHit(OwnerASC, Spec, DamageValue);
		}

		// 맞았다는 알림은 가드 판정 뒤, 피해 0 확인 앞 - 패링(피해 0)도 맞은 것이다. 무기 상태가 이것으로 전투에 들어간다.
		if (UVBGameplayMessageSubsystem* Bus = UVBGameplayMessageSubsystem::Get(OwnerASC.GetAvatarActor()))
		{
			FVBHitReceivedMessage Hit;
			Hit.Victim = OwnerASC.GetAvatarActor();
			Bus->BroadcastMessage(VBGameplayTags::Message_Combat_HitReceived, Hit);
		}

		return Result;
	}
}

UVBHealthAttributeSet::UVBHealthAttributeSet()
{
	// 기본값은 Init...()로 설정, GE_InitStats에서 외부 설정
	// 여기서는 안전한 기본값만 지정
	InitHealth(0.0f);
	InitMaxHealth(0.0f);
	InitShield(0.0f);
	InitMaxShield(0.0f);
}

void UVBHealthAttributeSet::PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue)
{
	Super::PreAttributeChange(Attribute, NewValue);
	
	// 주의 : PreAttributeChange는 CurrentValue에만 영향
	// BaseValue 클램핑이 필요하면 PostGameplayEffectExecute에서 처리
	
	if (Attribute == GetHealthAttribute())
	{
		// Health는 0 ~ MaxHealth 범위
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxHealth());
	}
	else if (Attribute == GetMaxHealthAttribute())
	{
		// MaxHealth는 1 이상 (0이면 나누기 에러 가능)
		NewValue = FMath::Max(1.0f, NewValue);
	}
	else if (Attribute == GetShieldAttribute())
	{
		ClampAttributeOnChange(Attribute, NewValue, 0.0f, GetMaxShield());
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		// MaxShield는 1 이상 (0이면 나누기 에러 가능)
		NewValue = FMath::Max(1.0f, NewValue);
	}

}

void UVBHealthAttributeSet::PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue)
{
	Super::PostAttributeChange(Attribute, OldValue, NewValue);

	// 상한이 바뀌면 현재값의 비율을 지킨다(가득 찬 채 장비를 입으면 여전히 가득). 순간, 무한 GE 어느 쪽이든 여기서 한 번 불린다.
	// 쓰기는 서버만 한다. OldValue 0 은 초기화 GE 가 처음 상한을 세우는 순간이라 비율이 없어 건드리지 않는다.
	const AActor* Owner = GetOwningActor();
	if (!Owner || !Owner->HasAuthority() || OldValue <= 0.0f || FMath::IsNearlyEqual(OldValue, NewValue))
	{
		return;
	}
	// 죽음의 입구. 모든 쓰기(효과, SetNumericAttributeBase 의 저장 복원, 디버그 키의 ApplyModToAttribute)가 여기를 지나고,
	//   속성 변경 델리게이트(조우 문의 피해로 깨기, 보스 페이즈)보다 앞이다. 효과 밖의 쓰기는 죽인 자가 없다.
	if (Attribute == GetHealthAttribute() && NewValue <= 0.0f)
	{
		FVBDeathFacts Facts = ExecutingDeathFacts.IsEmpty() ? FVBDeathFacts() : ExecutingDeathFacts.Last();
		Facts.DeadActor = GetAvatarActorFromOwner();
		OnOutOfHealth.Broadcast(Facts);
		return;
	}

	if (Attribute == GetMaxHealthAttribute())
	{
		SetHealth(FMath::Clamp(GetHealth() / OldValue * NewValue, 0.0f, NewValue));
	}
	else if (Attribute == GetMaxShieldAttribute())
	{
		SetShield(FMath::Clamp(GetShield() / OldValue * NewValue, 0.0f, NewValue));
	}
}

bool UVBHealthAttributeSet::PreGameplayEffectExecute(FGameplayEffectModCallbackData& Data)
{
	if (!Super::PreGameplayEffectExecute(Data))
	{
		return false;
	}
	// 받지 않는 타격의 판정은 UVBAbilitySystemStatics::ShouldIgnoreHit 하나가 맡는다 - 기절 게이지(UVBCombatAttributeSet)도 같은 것을 부른다.
	//   여기서 false 면 엔진이 적용, Post 를 부르지 않는다(GameplayEffect.cpp InternalExecuteMod) - 반응, 넉백, 창 요청, 섬광도 안 나간다.
	const TCHAR* Reason = nullptr;
	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute() && Data.EvaluatedData.Magnitude > 0.0f
		&& UVBAbilitySystemStatics::ShouldIgnoreHit(GetOwningAbilitySystemComponent(), Data.EffectSpec, &Reason))
	{
		VB_LOG(Log, "%s: %s 에게 온 피해 %.0f 을 받지 않는다", Reason, *GetNameSafe(GetAvatarActorFromOwner()), Data.EvaluatedData.Magnitude);
		return false;
	}
	// 엔진은 true 를 돌려준 Pre 마다 Post 를 부른다(GameplayEffect.cpp InternalExecuteMod) - 그래서 여기서 쌓고 Post 끝에서 꺼낸다.
	FVBDeathFacts& Facts = ExecutingDeathFacts.AddDefaulted_GetRef();
	Facts.Instigator = Data.EffectSpec.GetContext().GetInstigator();
	Facts.bMedical = UVBAbilitySystemStatics::IsFinisherDamage(Data.EffectSpec);
	if (const FVBGameplayEffectContext* Context = FVBGameplayEffectContext::Get(Data.EffectSpec.GetContext()))
	{
		Facts.Motion = Context->GetDeathMotion();
	}

	return true;
}

void UVBHealthAttributeSet::PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data)
{
	Super::PostGameplayEffectExecute(Data);
	ON_SCOPE_EXIT
	{
		if (!ExecutingDeathFacts.IsEmpty())
		{
			ExecutingDeathFacts.Pop(EAllowShrinking::No);
		}
	};

	if (Data.EvaluatedData.Attribute == GetIncomingDamageAttribute())
	{
		ProcessIncomingDamage(Data);
	}

	if (Data.EvaluatedData.Attribute == GetIncomingHealingAttribute())
	{
		ProcessIncomingHealing();
	}

	// 상한 변화의 비율 지키기는 PostAttributeChange 하나가 한다(순간, 무한 GE 모두). 여기 두면 순간 GE 에서 두 번 돈다.
}

void UVBHealthAttributeSet::ProcessIncomingDamage(const FGameplayEffectModCallbackData& Data)
{
	float DamageValue = GetIncomingDamage();
	// Meta Attribute 초기화 (다음 계산을 위해)
	SetIncomingDamage(0.0f);

	UAbilitySystemComponent* OwnerASC = GetOwningAbilitySystemComponent();
	const FVBCombatChargeSnapshot Charge = FVBCombatChargeSnapshot::Capture(OwnerASC, Data.EffectSpec);
	float AppliedChargeDamage = 0.0f;

	// 「이 피해를 받지 않는다」(회피, 처형 중, 처형 창의 처형 아닌 피해)는 PreGameplayEffectExecute 가 이미 걸렀다 -
	//   여기 오는 피해는 받는 피해다. 가드, 실드보다 앞이라 감쇠 계산을 안 거친다.
	FVBGuardResult Guard;
	Guard.Damage = DamageValue;
	if (DamageValue > 0.0f && OwnerASC)
	{
		Guard = ResolveGuardAndAnnounceHit(*OwnerASC, Data.EffectSpec, DamageValue);
	}

	DamageValue = Guard.Damage;

	// 받는 쪽 배수(DamageTakenScale)와 난이도는 이미 곱해져 왔다 - 계산은 UVBDamageExecution 하나가 맡는다.
	//   피해 GE 다섯이 모두 그 실행을 지난다(자산 시험 VB.Unit.Damage.EffectAssets 가 짝을 지킨다).
	if (DamageValue > 0.0f)
	{
		LogIncomingDamage(Data, DamageValue);
		const float RemainingDamage = TakeShieldThenHealth(DamageValue, AppliedChargeDamage);

		// 받아들인 피해의 반응(처형 창 -> AI 감지 -> 반응, 넉백 -> 섬광)은 VBDamageResponse 가 순서대로 낸다.
		//   SetHealth 뒤여야 한다 - 사망 처리가 SetHealth 안에서 끝나야 치명타의 반응이 State.Dead 로 막힌다.
		if (OwnerASC)
		{
			VBDamageResponse::Respond({ *OwnerASC, Data.EffectSpec, DamageValue, RemainingDamage, GetHealth(), GetMaxHealth(),
				Guard.bGuarded });
		}
	}

	Charge.PublishResolved(this, Guard.bGuarded, Guard.bPerfectParry, AppliedChargeDamage);
}

void UVBHealthAttributeSet::LogIncomingDamage(const FGameplayEffectModCallbackData& Data, float DamageValue) const
{
	// 받는 쪽 로그 1줄. 보내는 쪽(VBGA_MeleeAttackBase)의 곱셈 사슬 로그와 대조하면 스펙과 실제의 차이가 로그만으로 보인다.
	// 게이트/실드 처리 직후 값이라 감쇠까지 반영되고, 대상과 출처 GE 를 함께 남겨 누가 때렸는지 좁힌다.
	const AActor* DamagedActor = GetAvatarActorFromOwner();
	const UGameplayEffect* SourceDef = Data.EffectSpec.Def;
	VB_LOG(Log, "피격[%s]: 데미지 %.1f (출처 %s) / Shield %.1f / Health %.1f→%.1f (Max %.1f)",
		DamagedActor ? *DamagedActor->GetName() : TEXT("Unknown"),
		DamageValue, SourceDef ? *SourceDef->GetName() : TEXT("Unknown"),
		GetShield(), GetHealth(),
		FMath::Max(0.0f, GetHealth() - FMath::Max(0.0f, DamageValue - GetShield())),
		GetMaxHealth());
}

float UVBHealthAttributeSet::TakeShieldThenHealth(float DamageValue, float& AppliedChargeDamage)
{
	float RemainingDamage = DamageValue;

	// 1단계 : Shield먼저 차감
	if (GetShield() > 0.0f)
	{
		const float ShieldAbsorb = FMath::Min(GetShield(), RemainingDamage);
		AppliedChargeDamage += ShieldAbsorb;
		SetShield(GetShield() - ShieldAbsorb);
		RemainingDamage -= ShieldAbsorb;
	}

	// 2단계 : 남은 데미지를 Health에서 차감
	if (RemainingDamage > 0.0f)
	{
		const float NewHealth = FMath::Max(ResolveHealthFloor(), GetHealth() - RemainingDamage);
		// 0 을 넘으면 이 쓰기 안에서(PostAttributeChange) 죽음이 끝난다. 죽인 자는 Pre 에서 든 이 효과의 Facts 다.
		AppliedChargeDamage += FMath::Max(0.0f, GetHealth() - NewHealth);
		SetHealth(NewHealth);
	}

	return RemainingDamage;
}

float UVBHealthAttributeSet::ResolveHealthFloor() const
{
	// 게임 규칙, 촬영 설정, 개발 치트는 같은 피해 하한을 사용한다.
	const APawn* VictimPawn = Cast<APawn>(GetAvatarActorFromOwner());
	const AVBGameMode* GameMode = VictimPawn && VictimPawn->GetWorld()
		? VictimPawn->GetWorld()->GetAuthGameMode<AVBGameMode>() : nullptr;
	const AWorldSettings* Settings = VictimPawn && VictimPawn->GetWorld()
		? VictimPawn->GetWorld()->GetWorldSettings() : nullptr;
	static const FName NonLethalPlayerTag(TEXT("VB.Showcase.NonLethalPlayer"));
	bool bNoDeathCheat = false;
#if !UE_BUILD_SHIPPING
	bNoDeathCheat = CVarPlayerNoDeath.GetValueOnGameThread() != 0;
#endif
	const bool bNonLethalPlayer = VictimPawn && VictimPawn->IsPlayerControlled()
		&& ((GameMode && GameMode->IsPlayerCombatNonLethal())
			|| (Settings && Settings->ActorHasTag(NonLethalPlayerTag)) || bNoDeathCheat)
		&& GetHealth() > 0.0f && GetMaxHealth() > 0.0f;
	return bNonLethalPlayer ? FMath::Min(1.0f, GetMaxHealth()) : 0.0f;
}

void UVBHealthAttributeSet::ProcessIncomingHealing()
{
	const float HealValue = GetIncomingHealing();
	SetIncomingHealing(0.0f);

	if (HealValue > 0.0f)
	{
		const float NewHealth = FMath::Min(GetMaxHealth(), GetHealth() + HealValue);
		SetHealth(NewHealth);
	}
}

void UVBHealthAttributeSet::GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);
	
	// REPNOTIFY_Always : 같은 값이 다시 세팅되어도 OnRep 호출
	// UI 동기화에 필요
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, Health,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, MaxHealth,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, Shield,
		COND_None, REPNOTIFY_Always);
	DOREPLIFETIME_CONDITION_NOTIFY(UVBHealthAttributeSet, MaxShield,
		COND_None, REPNOTIFY_Always);
}

void UVBHealthAttributeSet::OnRep_Health(const FGameplayAttributeData& OldHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, Health, OldHealth);
}

void UVBHealthAttributeSet::OnRep_MaxHealth(const FGameplayAttributeData& OldMaxHealth)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, MaxHealth, OldMaxHealth);
}

void UVBHealthAttributeSet::OnRep_Shield(const FGameplayAttributeData& OldShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, Shield, OldShield);
}

void UVBHealthAttributeSet::OnRep_MaxShield(const FGameplayAttributeData& OldMaxShield)
{
	GAMEPLAYATTRIBUTE_REPNOTIFY(UVBHealthAttributeSet, MaxShield, OldMaxShield);
}
