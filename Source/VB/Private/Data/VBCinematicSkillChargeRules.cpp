#include "Data/VBCinematicSkillChargeRules.h"

#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "GameplayEffect.h"
#include "Data/VBCinematicSkillDefinition.h"
#include "Interaction/VBSetupValidation.h"

bool UVBCinematicSkillChargeRules::IsFull(float Current, float Maximum)
{
	return FMath::IsFinite(Current) && FMath::IsFinite(Maximum) && Maximum > 0.0f && Current >= Maximum;
}

float UVBCinematicSkillChargeRules::GetGain(EVBCombatChargeReason Reason) const
{
	switch (Reason)
	{
	case EVBCombatChargeReason::Hit: return NormalHitGain;
	case EVBCombatChargeReason::Parry: return ParryGain;
	case EVBCombatChargeReason::Finisher: return FinisherGain;
	default: return 0.0f;
	}
}

FString UVBCinematicSkillChargeRules::ValidateCost(const UGameplayEffect* Effect, float Level)
{
	if (!Effect || Effect->DurationPolicy != EGameplayEffectDurationType::Instant || Effect->Modifiers.Num() != 1
		|| !Effect->Executions.IsEmpty())
	{
		return TEXT("CostGameplayEffectClass must have one Instant gauge modifier");
	}

	const FGameplayModifierInfo& Modifier = Effect->Modifiers[0];
	float Value = 0.0f;
	if (Modifier.Attribute != UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute() || Modifier.ModifierOp != EGameplayModOp::Override
		|| !Modifier.ModifierMagnitude.GetStaticMagnitudeIfPossible(Level, Value) || !FMath::IsFinite(Value) || Value != 0.0f)
	{
		return TEXT("CostGameplayEffectClass must override CinematicSkillGauge to zero");
	}

	return FString();
}

FString UVBCinematicSkillChargeRules::ValidateSetup() const
{
	if (!SkillDefinition || SkillDefinition->RequiredWeapon == EVBWeaponType::None)
	{
		return TEXT("SkillDefinition with a required weapon is missing");
	}

	if (!FMath::IsFinite(NormalHitGain) || NormalHitGain < 0 || !FMath::IsFinite(ParryGain) || ParryGain < 0
		|| !FMath::IsFinite(FinisherGain) || FinisherGain < 0)
	{
		return TEXT("Charge gains must be finite and non-negative");
	}

	if (RecentCreditLimit < 1)
	{
		return TEXT("RecentCreditLimit must be positive");
	}

	const UGameplayEffect* Gain = GainEffect ? GainEffect->GetDefaultObject<UGameplayEffect>() : nullptr;
	if (!Gain || Gain->DurationPolicy != EGameplayEffectDurationType::Instant || Gain->Modifiers.Num() != 1 || !Gain->Executions.IsEmpty())
	{
		return TEXT("GainEffect must have one Instant gauge modifier");
	}

	const FGameplayModifierInfo& Modifier = Gain->Modifiers[0];
	if (Modifier.Attribute != UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute() || Modifier.ModifierOp != EGameplayModOp::Additive
		|| Modifier.ModifierMagnitude.GetMagnitudeCalculationType() != EGameplayEffectMagnitudeCalculation::SetByCaller
		|| Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag != VBGameplayTags::Data_CinematicSkillGaugeGain)
	{
		return TEXT("GainEffect must add Data.CinematicSkillGaugeGain to CinematicSkillGauge");
	}

	if (ReadyFeedbackEffect)
	{
		const UGameplayEffect* Feedback = ReadyFeedbackEffect->GetDefaultObject<UGameplayEffect>();
		if (Feedback->DurationPolicy != EGameplayEffectDurationType::Infinite || !Feedback->Modifiers.IsEmpty()
			|| !Feedback->Executions.IsEmpty())
		{
			return TEXT("ReadyFeedbackEffect must be Infinite and must not modify attributes");
		}
	}

	return FString();
}

#if WITH_EDITOR
EDataValidationResult UVBCinematicSkillChargeRules::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult Base = Super::IsDataValid(Context);
	return VBReportSetupProblem(Context, *this, ValidateSetup(),
		Base == EDataValidationResult::Invalid ? Base : EDataValidationResult::Valid);
}
#endif
