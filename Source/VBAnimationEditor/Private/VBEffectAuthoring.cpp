#include "VBEffectAuthoring.h"

#include "Distributions/DistributionFloat.h"
#include "Distributions/DistributionVector.h"
#include "Materials/MaterialInterface.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleEmitter.h"
#include "Particles/ParticleLODLevel.h"
#include "Particles/ParticleModule.h"
#include "Particles/ParticleModuleRequired.h"
#include "Particles/TypeData/ParticleModuleTypeDataBase.h"
#include "UObject/UnrealType.h"

DEFINE_LOG_CATEGORY_STATIC(LogVBEffectAuthoring, Log, All);

namespace
{
	template<typename TRawDistribution>
	bool RebuildDistributionCache(UParticleModule* Module, const FStructProperty* Property)
	{
		TRawDistribution* Raw = Property->ContainerPtrToValuePtr<TRawDistribution>(Module);
		if (!IsValid(Raw->Distribution.Get()))
		{
			return false;
		}

		Module->Modify();
		Raw->Distribution->bIsDirty = true;
		Raw->Initialize();
		return true;
	}
}

bool UVBEffectAuthoring::RebuildCascadeDistribution(UParticleModule* Module, FName PropertyName)
{
	if (!IsValid(Module))
	{
		UE_LOG(LogVBEffectAuthoring, Error, TEXT("Cascade distribution rebuild requires a valid particle module"));
		return false;
	}

	const FStructProperty* Property = FindFProperty<FStructProperty>(Module->GetClass(), PropertyName);
	bool bRebuilt = false;
	if (Property && Property->Struct == FRawDistributionFloat::StaticStruct())
	{
		bRebuilt = RebuildDistributionCache<FRawDistributionFloat>(Module, Property);
	}
	else if (Property && Property->Struct == FRawDistributionVector::StaticStruct())
	{
		bRebuilt = RebuildDistributionCache<FRawDistributionVector>(Module, Property);
	}

	if (!bRebuilt)
	{
		UE_LOG(LogVBEffectAuthoring, Error, TEXT("Cascade distribution rebuild requires a valid float/vector distribution: %s.%s"),
			*Module->GetPathName(), *PropertyName.ToString());
	}

	return bRebuilt;
}

TArray<FString> UVBEffectAuthoring::DescribeCascade(UParticleSystem* System)
{
	TArray<FString> Result;
	if (!IsValid(System))
	{
		UE_LOG(LogVBEffectAuthoring, Error, TEXT("Cascade readback requires a valid particle system"));
		return Result;
	}

	for (const UParticleEmitter* Emitter : System->Emitters)
	{
		if (!Emitter)
		{
			continue;
		}

		for (int32 Index = 0; Index < Emitter->LODLevels.Num(); ++Index)
		{
			const UParticleLODLevel* LOD = Emitter->LODLevels[Index];
			if (!LOD)
			{
				continue;
			}

			TArray<FString> Modules;
			for (const UParticleModule* Module : LOD->Modules)
			{
				if (Module)
				{
					Modules.Add(FString::Printf(TEXT("%s:%d"), *Module->GetClass()->GetName(), Module->bEnabled ? 1 : 0));
				}
			}

			const UParticleModuleRequired* Required = LOD->RequiredModule;
			Result.Add(FString::Printf(TEXT("emitter=%s lod=%d enabled=%d type=%s material=%s local=%d period=%.3f loops=%d modules=%s"),
				*Emitter->EmitterName.ToString(), Index, LOD->bEnabled ? 1 : 0,
				LOD->TypeDataModule ? *LOD->TypeDataModule->GetClass()->GetName() : TEXT("Sprite"),
				Required ? *GetPathNameSafe(Required->Material.Get()) : TEXT("None"),
				Required && Required->bUseLocalSpace ? 1 : 0, Required ? Required->EmitterDuration : 0.0f,
				Required ? Required->EmitterLoops : 0, *FString::Join(Modules, TEXT(","))));
		}
	}

	return Result;
}
