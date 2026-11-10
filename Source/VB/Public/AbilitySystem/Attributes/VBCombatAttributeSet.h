// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystemComponent.h"
#include "Engine/TimerHandle.h"
#include "VBAttributeSetBase.h"
#include "VBCombatAttributeSet.generated.h"

/**
 * 전투 Attribute(스태미나, 공격력, 기절 게이지, 피해, 넉백 배수).
 */
UCLASS()
class VB_API UVBCombatAttributeSet : public UVBAttributeSetBase
{
	GENERATED_BODY()

public:
	UVBCombatAttributeSet();
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, CinematicSkillGauge)
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxCinematicSkillGauge)

	// Stamina
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Stamina, Category="VB|Combat")
	FGameplayAttributeData Stamina;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, Stamina)

	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxStamina, Category="VB|Combat")
	FGameplayAttributeData MaxStamina;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxStamina)

	// AttackPower
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_AttackPower, Category="VB|Combat")
	FGameplayAttributeData AttackPower;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, AttackPower)

	// 남은 기절 게이지. 0 에 닿으면 Event.Combat.PoiseBroken 을 쏘고 MaxPoise 로 리필한다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_Poise, Category="VB|Combat")
	FGameplayAttributeData Poise;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, Poise)

	// 기절 게이지 최대치. 0 이하 = 경직 시스템을 쓰지 않음(1 로 올려 치지 않는다).
	// 적이 잘 안 끊기는 정도는 슈퍼아머 태그가 아니라 이 값으로 표현한다.
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxPoise, Category="VB|Combat")
	FGameplayAttributeData MaxPoise;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, MaxPoise)

	// 마지막 게이지 피해 뒤 회복 시작까지의 초. 서버만 읽어 복제하지 않는다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData PoiseRecoveryDelaySeconds;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, PoiseRecoveryDelaySeconds)

	// 회복 중 초당 되돌리는 게이지 양. 0 = 회복 없음.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData PoiseRecoveryPerSecond;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, PoiseRecoveryPerSecond)

	// 메타 속성(비복제). GE_PhysicalDamage 가 SetByCaller(Data.PoiseDamage)로 더하면 PostGameplayEffectExecute 가 소비한다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData IncomingPoiseDamage;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, IncomingPoiseDamage)

	// 피해 GE 의 기본 피해 입력 칸(공격자 쪽). 값은 늘 0 이고 실행 정의의 계산 수정값이 얹혀 UVBDamageExecution 이 읽는다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData BaseDamage;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, BaseDamage)

	// 받는 피해 배수(1.0 = 그대로). UVBDamageExecution 이 곱한다. 그로기 중 GE 가 올린다. 비복제.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData DamageTakenScale;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, DamageTakenScale)

	// 주는 근접 피해 배수(손에 든 무기의 강화). 1.0 = 그대로. 무기 강화 효과가 덮어쓴다. 비복제.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData WeaponDamageScale;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, WeaponDamageScale)

	// 반응 등급 저항. 공격 등급을 이 단계 수만큼 깎는다(반올림 정수). 0 = 그대로. 비복제.
	// 기절 게이지(쌓여서 깨지는 양)와 축이 다르다 - 이것은 한 대가 얼마나 먹히나다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData ImpactResistance;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, ImpactResistance)

	// 받는 넉백 배수(1.0 = 그대로). 물리 넉백에만 걸린다 - 루트모션 클립이면 거리는 클립이 정한다. 비복제.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	FGameplayAttributeData KnockbackTakenScale;
	ATTRIBUTE_ACCESSORS(UVBCombatAttributeSet, KnockbackTakenScale)

protected:
	virtual void PreAttributeBaseChange(const FGameplayAttribute& Attribute, float& NewValue) const override;
	virtual void PostAttributeChange(const FGameplayAttribute& Attribute, float OldValue, float NewValue) override;
	virtual void PreAttributeChange(const FGameplayAttribute& Attribute, float& NewValue) override;
	virtual void GetLifetimeReplicatedProps(TArray<class FLifetimeProperty>& OutLifetimeProps) const override;

	// 받지 않는 타격(ShouldIgnorePoiseHit)은 기절 게이지를 깎지 않는다.
	virtual bool PreGameplayEffectExecute(struct FGameplayEffectModCallbackData& Data) override;
	virtual void PostGameplayEffectExecute(const struct FGameplayEffectModCallbackData& Data) override;

	UFUNCTION()
	void OnRep_Stamina(const FGameplayAttributeData& OldStamina);
	UFUNCTION()
	void OnRep_MaxStamina(const FGameplayAttributeData& OldMaxStamina);
	UFUNCTION()
	void OnRep_AttackPower(const FGameplayAttributeData& OldAttackPower);
	UFUNCTION()
	void OnRep_Poise(const FGameplayAttributeData& OldPoise);
	UFUNCTION()
	void OnRep_MaxPoise(const FGameplayAttributeData& OldMaxPoise);

	float CachedMaxStamina = 0.0f;
private:
	bool ClampCinematicGauge(const FGameplayAttribute& Attribute, float& Value) const;

	// IncomingPoiseDamage 하나를 소비한다: 게이지를 깎고, 다 비면 파탄으로 전환한다(서버).
	void ProcessIncomingPoiseDamage(const struct FGameplayEffectModCallbackData& Data);

	// 게이지를 다시 채우고 파탄 사건을 쏜다(서버).
	void BreakPoise(UAbilitySystemComponent& OwnerASC, const struct FGameplayEffectModCallbackData& Data);

	// 게이지 피해 시각을 남기고 회복 타이머를 건다(서버).
	void NotePoiseDamage();
	// 회복 타이머 한 번. 지연이 지났고 창이 꺼져 있으면 게이지를 되돌리고, 다 차거나 회복이 꺼지면 멈춘다.
	void TickPoiseRecovery();

	// 마지막으로 게이지 피해를 받은 월드 시각(서버).
	double LastPoiseDamageTime = 0.0;
	// 마지막으로 회복을 셈한 월드 시각(서버).
	double LastPoiseRecoveryTime = 0.0;
	// 회복 타이머. 게이지가 덜 찬 동안만 돈다.
	FTimerHandle PoiseRecoveryTimer;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_CinematicSkillGauge, Category="VB|Combat", meta=(AllowPrivateAccess="true"))
	FGameplayAttributeData CinematicSkillGauge;
	UPROPERTY(BlueprintReadOnly, ReplicatedUsing=OnRep_MaxCinematicSkillGauge, Category="VB|Combat", meta=(AllowPrivateAccess="true"))
	FGameplayAttributeData MaxCinematicSkillGauge;
	UFUNCTION()
	void OnRep_CinematicSkillGauge(const FGameplayAttributeData& OldValue);
	UFUNCTION()
	void OnRep_MaxCinematicSkillGauge(const FGameplayAttributeData& OldValue);

};
