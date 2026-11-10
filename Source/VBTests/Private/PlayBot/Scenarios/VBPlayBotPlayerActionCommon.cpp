#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"

namespace VBPlayBotPlayerActions
{
bool Positive(const FVBPlayBotContext& Ctx, const TCHAR* Key, double& Value, FString& Error)
{
	Value = Ctx.Config->GetDouble(Key, 0.0);
	if (FMath::IsFinite(Value) && Value > 0.0)
	{
		return true;
	}

	Error = FString::Printf(TEXT("[%s] %s must be finite and positive"), *Ctx.Config->Section, Key);
	return false;
}

bool Has(const FVBPlayBotContext& Ctx, const FGameplayTag& Tag)
{
	return Ctx.PlayerASC && Ctx.PlayerASC->HasMatchingGameplayTag(Tag);
}

bool Neutral(const FVBPlayBotContext& Ctx)
{
	const UCharacterMovementComponent* Move = Ctx.Player->GetCharacterMovement();
	return FVBPlayBotContext::IsGrounded(Ctx.Player) && FVBPlayBotContext::RawMoveInput(Ctx.Player).IsNearlyZero()
		&& Move && Move->GetCurrentAcceleration().IsNearlyZero() && Ctx.Player->GetVelocity().IsNearlyZero();
}

double Health(const AActor* Actor)
{
	const UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Actor);
	return ASC ? ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) : 0.0;
}

}
