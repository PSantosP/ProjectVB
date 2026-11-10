// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBParryConfig.h"

#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameplayEffect.h"

FString UVBParryConfig::GetPoiseEffectProblem(TSubclassOf<UGameplayEffect> Effect)
{
	const UGameplayEffect* Definition = Effect ? Effect->GetDefaultObject<UGameplayEffect>() : nullptr;
	if (!Definition)
	{
		return TEXT("효과 클래스가 비었다");
	}
	// 받아 주는 모양은 하나다: 즉시, 수정값 하나, IncomingPoiseDamage 에 SetByCaller Data.PoiseDamage 를 더함, 계산 없음.
	//   다른 모양은 모두 패링이 게이지 말고 다른 것을 바꾼다 - 그래서 무엇이 있나가 아니라 이 모양인가를 본다.
	const FString Name = GetNameSafe(Effect);
	if (Definition->DurationPolicy != EGameplayEffectDurationType::Instant)
	{
		return FString::Printf(TEXT("%s 가 즉시 효과가 아니다 - 기간 · 무한 효과는 패링 뒤에도 남는다"), *Name);
	}
	// 피해 계산은 공격하는 쪽(여기서는 패링한 쪽)의 BaseDamage 를 체력 피해로 낸다 - 게이지만 채워야 하는 효과에 달면 패링이 공격이 된다.
	if (!Definition->Executions.IsEmpty())
	{
		return FString::Printf(TEXT("%s 에 계산(Execution)이 있다 - 패링한 쪽의 기본 피해가 공격자 체력에 들어간다"), *Name);
	}

	if (Definition->Modifiers.Num() != 1)
	{
		return FString::Printf(TEXT("%s 의 수정값이 %d개다 - 하나(IncomingPoiseDamage)여야 한다, 더 있으면 체력 · 게이지를 따로 바꾼다"), *Name,
			Definition->Modifiers.Num());
	}

	const FGameplayModifierInfo& Modifier = Definition->Modifiers[0];
	if (Modifier.Attribute != UVBCombatAttributeSet::GetIncomingPoiseDamageAttribute() || Modifier.ModifierOp != EGameplayModOp::Additive)
	{
		return FString::Printf(TEXT("%s 의 수정값이 IncomingPoiseDamage 더하기가 아니다(%s)"), *Name, *Modifier.Attribute.GetName());
	}

	if (Modifier.ModifierMagnitude.GetMagnitudeCalculationType() != EGameplayEffectMagnitudeCalculation::SetByCaller
		|| Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag != VBGameplayTags::Data_PoiseDamage)
	{
		return FString::Printf(TEXT("%s 의 양이 SetByCaller Data.PoiseDamage 가 아니다 - ParryPoiseDamage 가 실리지 않는다"), *Name);
	}

	return FString();
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBParryConfig"

EDataValidationResult UVBParryConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	// 양이 0 이면 게이지를 채우지 않는 설정이라 효과는 묻지 않는다. 문구는 GetPoiseEffectProblem 하나가 관리한다(실제 패링도 같은 문구를 쓴다).
	const FString Problem = ParryPoiseDamage > 0.0f ? GetPoiseEffectProblem(ParryPoiseEffect) : FString();
	if (!Problem.IsEmpty())
	{
		Context.AddError(FText::Format(LOCTEXT("VBParry_BadPoiseEffect",
				"ParryPoiseDamage({0}) 가 있는데 ParryPoiseEffect 를 쓸 수 없습니다: {1}. GE_ParryPoise 를 할당하거나 ParryPoiseDamage 를 0 으로 두세요."),
			FText::AsNumber(ParryPoiseDamage), FText::FromString(Problem)));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
