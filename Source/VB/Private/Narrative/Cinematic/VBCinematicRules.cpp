#include "Narrative/Cinematic/VBCinematicRules.h"

#include "Engine/World.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Cinematic/VBCinematicDirectorSubsystem.h"
#include "VBCinematicDefinition.h"
#include "VB/VB.h"

void FVBEffect_PlayCinematic::Apply(const FVBRuleContext& Context) const
{
	UVBCinematicDirectorSubsystem* Director = IsValid(Context.World) && Context.CanApplyEffects()
		? Context.World->GetSubsystem<UVBCinematicDirectorSubsystem>() : nullptr;
	if (!Director)
	{
		Context.OnDeferredClosed.ExecuteIfBound(false);
		return;
	}

	const FVBGameCinematicStartResult Result = Director->RequestPlay(
		Definition, Context.Instigator, PlayerBindingTag, OnCompletedEffects, Context.Counterpart, Context.OnDeferredClosed, OnHandoffEffects);
	if (Result.Status != EVBGameCinematicStartStatus::Accepted)
	{
		VB_LOG(Warning, "Cinematic request refused: %s (%s)", LexToString(Result.Status), *GetNameSafe(Definition));
		Context.OnDeferredClosed.ExecuteIfBound(false);
	}
}

const UObject* FVBEffect_PlayCinematic::GetPlayedScene() const
{
	return Definition;
}

const UObject* FVBCondition_CinematicCompleted::GetTransientSceneRead() const
{
	return Definition;
}

bool FVBCondition_CinematicCompleted::Evaluate(const FVBRuleContext& Context) const
{
	if (!IsValid(Context.World) || !IsValid(Context.Instigator) || !IsValid(Definition))
	{
		return false;
	}

	const UVBCinematicDirectorSubsystem* Director = Context.World->GetSubsystem<UVBCinematicDirectorSubsystem>();
	return Director && Director->HasCompletedFor(Context.Instigator->GetPawn(), Definition);
}
