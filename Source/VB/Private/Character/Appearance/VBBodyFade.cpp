#include "Character/Appearance/VBBodyFade.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBBodyFadeConfig.h"
#include "Engine/SkeletalMesh.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "VB/VB.h"

uint64 FVBBodyFade::Begin(const TArray<USkeletalMeshComponent*>& Meshes, const UVBBodyFadeConfig* InConfig, float InDuration)
{
	Reset();
	if (!InConfig || !FMath::IsFinite(InDuration) || InDuration <= 0.0f || Meshes.IsEmpty())
	{
		return 0;
	}

	const TArray<FText> Problems = InConfig->FindProblems();
	if (!Problems.IsEmpty())
	{
		for (const FText& Problem : Problems)
		{
			VB_LOG(Error, "Body fade: %s", *Problem.ToString());
		}

		return 0;
	}

	TArray<FVBBodyFadeSlot> Prepared;
	for (USkeletalMeshComponent* Mesh : Meshes)
	{
		if (!IsValid(Mesh) || !Mesh->IsRegistered() || !Mesh->GetSkeletalMeshAsset())
		{
			return 0;
		}

		for (int32 Index = 0; Index < Mesh->GetNumMaterials(); ++Index)
		{
			UMaterialInterface* Original = Mesh->GetMaterial(Index);
			UMaterialInterface* Variant = InConfig->FindVariant(Original);
			if (!Variant)
			{
				VB_LOG(Error, "Body fade has no compatible variant for %s slot %d (%s)", *GetNameSafe(Mesh), Index, *GetNameSafe(Original));
				return 0;
			}

			UMaterialInstanceDynamic* Applied = UMaterialInstanceDynamic::Create(Variant, Mesh);
			if (!Applied)
			{
				return 0;
			}

			Applied->CopyMaterialUniformParameters(Original);
			Applied->SetScalarParameterValue(UVBBodyFadeConfig::CoverageParameter, 1.0f);
			FVBBodyFadeSlot& Slot = Prepared.AddDefaulted_GetRef();
			Slot.Mesh = Mesh;
			Slot.Asset = Mesh->GetSkeletalMeshAsset();
			Slot.Original = Original;
			Slot.Applied = Applied;
			Slot.Index = Index;
		}
	}

	if (Prepared.IsEmpty())
	{
		return 0;
	}

	Slots = MoveTemp(Prepared);
	Config = InConfig;
	Age = 0.0f;
	Duration = InDuration;
	ActiveRequest = ++NextRequest;
	if (ActiveRequest == 0)
	{
		ActiveRequest = ++NextRequest;
	}

	for (const FVBBodyFadeSlot& Slot : Slots)
	{
		Slot.Mesh->SetMaterial(Slot.Index, Slot.Applied);
	}

	return ActiveRequest;
}

void FVBBodyFade::End(uint64 Request)
{
	if (Request != 0 && Request == ActiveRequest)
	{
		Reset();
	}
}

void FVBBodyFade::Reset()
{
	for (const FVBBodyFadeSlot& Slot : Slots)
	{
		USkeletalMeshComponent* Mesh = Slot.Mesh.Get();
		if (Mesh && Mesh->GetSkeletalMeshAsset() == Slot.Asset.Get() && Mesh->GetMaterial(Slot.Index) == Slot.Applied)
		{
			Mesh->SetMaterial(Slot.Index, Slot.Original);
		}
	}

	Slots.Reset();
	Config = nullptr;
	ActiveRequest = 0;
	Age = Duration = 0.0f;
}

void FVBBodyFade::Tick(float DeltaTime)
{
	if (!IsActive())
	{
		return;
	}

	for (const FVBBodyFadeSlot& Slot : Slots)
	{
		const USkeletalMeshComponent* Mesh = Slot.Mesh.Get();
		if (!Mesh || !Mesh->IsRegistered() || !Mesh->IsVisible() || Mesh->bHiddenInGame
			|| !Mesh->GetOwner() || Mesh->GetOwner()->IsHidden() || Mesh->GetSkeletalMeshAsset() != Slot.Asset.Get()
			|| Mesh->GetMaterial(Slot.Index) != Slot.Applied)
		{
			Reset();
			return;
		}
	}

	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	Age += DeltaTime;
	if (Age >= Duration)
	{
		Reset();
		return;
	}

	const float Coverage = Config->CoverageAt(Age / Duration);
	for (const FVBBodyFadeSlot& Slot : Slots)
	{
		Slot.Applied->SetScalarParameterValue(UVBBodyFadeConfig::CoverageParameter, Coverage);
	}
}
