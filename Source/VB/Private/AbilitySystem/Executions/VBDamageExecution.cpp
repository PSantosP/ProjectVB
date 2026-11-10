// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Executions/VBDamageExecution.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "GameFramework/PlayerState.h"
#include "VB/VB.h"

namespace
{
	struct FVBDamageCaptures
	{
		// 공격자 기본 피해: 스냅샷 - 스펙을 만든 순간의 값(실행 정의의 계산 수정값이 그 위에 얹힌다).
		FGameplayEffectAttributeCaptureDefinition BaseDamage;
		// 받는 피해 배수: 적용 순간의 피해자 값.
		FGameplayEffectAttributeCaptureDefinition DamageTakenScale;

		FVBDamageCaptures()
			: BaseDamage(UVBCombatAttributeSet::GetBaseDamageAttribute(), EGameplayEffectAttributeCaptureSource::Source, true)
			, DamageTakenScale(UVBCombatAttributeSet::GetDamageTakenScaleAttribute(), EGameplayEffectAttributeCaptureSource::Target, false)
		{
		}
	};

	const FVBDamageCaptures& Captures()
	{
		static const FVBDamageCaptures Statics;
		return Statics;
	}
}

UVBDamageExecution::UVBDamageExecution()
{
	RelevantAttributesToCapture.Add(Captures().BaseDamage);
	RelevantAttributesToCapture.Add(Captures().DamageTakenScale);
}

void UVBDamageExecution::Execute_Implementation(const FGameplayEffectCustomExecutionParameters& ExecutionParams,
	FGameplayEffectCustomExecutionOutput& OutExecutionOutput) const
{
	const FGameplayEffectSpec& Spec = ExecutionParams.GetOwningSpec();
	UAbilitySystemComponent* Victim = ExecutionParams.GetTargetAbilitySystemComponent();

	FAggregatorEvaluateParameters Evaluate;
	Evaluate.SourceTags = Spec.CapturedSourceTags.GetAggregatedTags();
	Evaluate.TargetTags = Spec.CapturedTargetTags.GetAggregatedTags();

	float Damage = 0.0f;
	if (!ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(Captures().BaseDamage, Evaluate, Damage))
	{
		// 공격자에 CombatSet 이 없으면 기본 피해가 0 이 된다 - 새 출처가 조용히 0 이 되지 않게 경고한다.
		const UAbilitySystemComponent* Source = ExecutionParams.GetSourceAbilitySystemComponent();
		VB_LOG(Warning, "피해 계산: %s 의 공격자 %s 에 BaseDamage(CombatSet) 가 없다 - 피해 0", *GetNameSafe(Spec.Def),
			*GetNameSafe(Source ? Source->GetOwnerActor() : nullptr));
	}

	Damage = FMath::Max(Damage, 0.0f);

	// 받는 피해 배수(DamageTakenScale). 세트가 없거나 1 이면 곱하지 않는다(없는 속성은 0 이라 곱하면 무적이 된다).
	// 로그 숫자는 가드 감쇠, 무효화 전의 값이다.
	if (Damage > 0.0f && Victim && Victim->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetDamageTakenScaleAttribute()))
	{
		float Scale = 1.0f;
		if (ExecutionParams.AttemptCalculateCapturedAttributeMagnitude(Captures().DamageTakenScale, Evaluate, Scale)
			&& !FMath::IsNearlyEqual(Scale, 1.0f))
		{
			VB_LOG(Log, "받는 피해 배수 x%.2f (가드 전) : %.1f -> %.1f", Scale, Damage, Damage * Scale);
			Damage *= Scale;
		}
	}

	// 난이도 배수. 받는 배수와 따로 곱한다(처형만 빼내려고). 처형, 정화 피해는 1.
	// 피해자가 플레이어인지는 ASC 의 Owner 가 PlayerState 인지로 판단한다. 적용 순간의 난이도를 읽는다.
	if (Damage > 0.0f && Victim)
	{
		if (const UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(Victim))
		{
			const bool bVictimIsPlayer = Cast<APlayerState>(Victim->GetOwnerActor()) != nullptr;
			const float Scale = Difficulty->GetDamageScale(bVictimIsPlayer, UVBAbilitySystemStatics::IsFinisherDamage(Spec));
			if (!FMath::IsNearlyEqual(Scale, 1.0f))
			{
				VB_LOG(Log, "난이도 %s 배수 x%.2f(%s, 가드 전) : %.1f -> %.1f", *Difficulty->GetLevel().ToString(), Scale,
					bVictimIsPlayer ? TEXT("플레이어가 받음") : TEXT("적이 받음"), Damage, Damage * Scale);
				Damage *= Scale;
			}
		}
	}

	// 체력 다음 경직 - 출력 순서가 적용 순서다.
	if (Damage > 0.0f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVBHealthAttributeSet::GetIncomingDamageAttribute(),
			EGameplayModOp::Additive, Damage));
	}

	const float Poise = Spec.GetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage,
		/*WarnIfNotFound=*/false, /*DefaultIfNotFound=*/0.0f);
	if (Poise > 0.0f)
	{
		OutExecutionOutput.AddOutputModifier(FGameplayModifierEvaluatedData(UVBCombatAttributeSet::GetIncomingPoiseDamageAttribute(),
			EGameplayModOp::Additive, Poise));
	}
}
