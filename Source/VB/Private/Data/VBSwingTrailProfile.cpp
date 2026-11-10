#include "Data/VBSwingTrailProfile.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/ParticleSystem.h"
#include "Particles/TypeData/ParticleModuleTypeDataAnimTrail.h"
#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

TArray<FString> FVBAnimTrailBinding::GatherValidationErrors() const
{
	TArray<FString> Errors;
	if (FirstSocket.IsNone() || SecondSocket.IsNone() || FirstSocket == SecondSocket)
	{
		Errors.Add(TEXT("AnimTrail requires two distinct mesh sockets"));
	}

	if (!FMath::IsFinite(WidthScale) || WidthScale <= 0.0f
		|| !FMath::IsFinite(MaxTailSeconds) || MaxTailSeconds <= 0.0f)
	{
		Errors.Add(TEXT("AnimTrail width and tail limit must be finite and positive"));
	}

	if (!IsValid(System))
	{
		Errors.Add(TEXT("AnimTrail requires a particle system"));
		return Errors;
	}

	int32 EnabledLODs = 0;
	for (const UParticleEmitter* Emitter : System->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		for (const UParticleLODLevel* LOD : Emitter->LODLevels)
		{
			if (!LOD || !LOD->bEnabled)
			{
				continue;
			}

			++EnabledLODs;
			if (!Cast<UParticleModuleTypeDataAnimTrail>(LOD->TypeDataModule))
			{
				Errors.Add(FString::Printf(TEXT("%s requires AnimTrail type data on every enabled LOD"), *System->GetPathName()));
			}

			if (!LOD->RequiredModule || !LOD->RequiredModule->Material)
			{
				Errors.Add(FString::Printf(TEXT("%s has an enabled LOD without a required material"), *System->GetPathName()));
			}
		}
	}

	if (EnabledLODs == 0)
	{
		Errors.Add(TEXT("AnimTrail system has no enabled emitter LOD"));
	}

	return Errors;
}

TArray<FString> UVBSwingTrailProfile::GatherValidationErrors() const
{
	TArray<FString> Errors;
	for (const auto& Entry : AnimTrails)
	{
		if (!Entry.Key.IsValid() || !Entry.Key.MatchesTag(VBGameplayTags::Melee_Shape))
		{
			Errors.Add(TEXT("AnimTrail binding requires a Melee.Shape tag"));
		}

		for (const FString& Error : Entry.Value.GatherValidationErrors())
		{
			Errors.Add(FString::Printf(TEXT("%s: %s"), *Entry.Key.ToString(), *Error));
		}
	}

	return Errors;
}

#if WITH_EDITOR
EDataValidationResult UVBSwingTrailProfile::IsDataValid(FDataValidationContext& Context) const
{
	const EDataValidationResult ParentResult = Super::IsDataValid(Context);
	const TArray<FString> Errors = GatherValidationErrors();
	for (const FString& Error : Errors)
	{
		Context.AddError(FText::FromString(Error));
	}

	return !Errors.IsEmpty() || ParentResult == EDataValidationResult::Invalid
		? EDataValidationResult::Invalid : EDataValidationResult::Valid;
}
#endif
