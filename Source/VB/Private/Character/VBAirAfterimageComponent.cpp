// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBAirAfterimageComponent.h"

#include "Character/Appearance/VBCharacterAppearanceComponent.h"
#include "CharacterCommon/VBMeshRefresh.h"
#include "Components/PoseableMeshComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/VBAirAfterimageConfig.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Rendering/SkeletalMeshLODRenderData.h"
#include "Rendering/SkeletalMeshRenderData.h"
#include "VB/VB.h"

UVBAirAfterimageComponent::UVBAirAfterimageComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.TickGroup = TG_PostUpdateWork;
}

UVBAirAfterimageComponent* UVBAirAfterimageComponent::FindOrAdd(AActor* Owner)
{
	if (!Owner || !Owner->GetWorld() || Owner->GetNetMode() == NM_DedicatedServer)
	{
		return nullptr;
	}

	if (UVBAirAfterimageComponent* Existing = Owner->FindComponentByClass<UVBAirAfterimageComponent>())
	{
		return Existing;
	}

	UVBAirAfterimageComponent* Added = NewObject<UVBAirAfterimageComponent>(Owner, NAME_None, RF_Transient);
	Owner->AddInstanceComponent(Added);
	Added->RegisterComponent();
	return Added;
}

USkeletalMeshComponent* UVBAirAfterimageComponent::PoseSourceFor(USkeletalMeshComponent* Visual)
{
	if (!Visual)
	{
		return nullptr;
	}

	USkinnedMeshComponent* Leader = Visual->LeaderPoseComponent.Get();
	return Leader ? Cast<USkeletalMeshComponent>(Leader) : Visual;
}

bool UVBAirAfterimageComponent::GatherSources(TArray<FSource>& OutSources, FString& OutError) const
{
	TArray<USkeletalMeshComponent*> Visible;
	UVBCharacterAppearanceComponent::GatherVisibleMeshes(GetOwner(), Visible);
	if (Visible.IsEmpty())
	{
		OutError = TEXT("No visible skeletal mesh is available");
		return false;
	}

	for (USkeletalMeshComponent* Mesh : Visible)
	{
		USkeletalMeshComponent* Pose = PoseSourceFor(Mesh);
		if (!Pose || !Pose->GetSkeletalMeshAsset())
		{
			OutError = FString::Printf(TEXT("Unsupported pose source for %s"), *GetNameSafe(Mesh));
			return false;
		}

		FSource& Source = OutSources.AddDefaulted_GetRef();
		Source.Visual = Mesh;
		Source.Pose = Pose;
		Source.Asset = Mesh->GetSkeletalMeshAsset();
		Source.PoseAsset = Pose->GetSkeletalMeshAsset();
	}

	return true;
}

bool UVBAirAfterimageComponent::SourcesRemainValid() const
{
	if (!GetOwner() || GetOwner()->IsHidden())
	{
		return false;
	}

	for (const FSource& Source : Sources)
	{
		USkeletalMeshComponent* Visual = Source.Visual.Get();
		const AActor* VisualOwner = Visual ? Visual->GetOwner() : nullptr;
		if (!Visual || !Source.Pose.IsValid() || Source.Pose->GetSkeletalMeshAsset() != Source.PoseAsset.Get()
			|| !Visual->IsRegistered() || !Visual->IsVisible() || Visual->bHiddenInGame
			|| !VisualOwner || VisualOwner->IsHidden()
			|| Visual->GetSkeletalMeshAsset() != Source.Asset.Get() || PoseSourceFor(Visual) != Source.Pose.Get())
		{
			return false;
		}
	}

	return !Sources.IsEmpty();
}

void UVBAirAfterimageComponent::BindAppearance()
{
	UVBCharacterAppearanceComponent* Appearance = GetOwner()->FindComponentByClass<UVBCharacterAppearanceComponent>();
	if (ObservedAppearance.Get() == Appearance)
	{
		return;
	}

	UnbindAppearance();
	ObservedAppearance = Appearance;
	if (Appearance)
	{
		AppearanceChangedHandle = Appearance->OnAppearanceChanged.AddUObject(this, &ThisClass::HandleAppearanceChanged);
	}
}

void UVBAirAfterimageComponent::UnbindAppearance()
{
	if (UVBCharacterAppearanceComponent* Appearance = ObservedAppearance.Get())
	{
		Appearance->OnAppearanceChanged.Remove(AppearanceChangedHandle);
	}

	AppearanceChangedHandle.Reset();
	ObservedAppearance.Reset();
}

void UVBAirAfterimageComponent::HandleAppearanceChanged(UVBCharacterAppearanceComponent* Appearance)
{
	ResetVisuals();
}

void UVBAirAfterimageComponent::AcquireMeshUpdates()
{
	for (const FSource& Source : Sources)
	{
		RefreshMeshes.AddUnique(Source.Pose);
		TickMeshes.AddUnique(Source.Pose);
		TickMeshes.AddUnique(Source.Visual);
	}

	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : RefreshMeshes)
	{
		FVBMeshRefresh::Acquire(Mesh.Get());
	}

	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : TickMeshes)
	{
		if (Mesh.IsValid())
		{
			AddTickPrerequisiteComponent(Mesh.Get());
		}
	}
}

void UVBAirAfterimageComponent::ReleaseMeshUpdates()
{
	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : TickMeshes)
	{
		if (Mesh.IsValid())
		{
			RemoveTickPrerequisiteComponent(Mesh.Get());
		}
	}

	for (const TWeakObjectPtr<USkeletalMeshComponent>& Mesh : RefreshMeshes)
	{
		FVBMeshRefresh::Release(Mesh.Get());
	}

	TickMeshes.Reset();
	RefreshMeshes.Reset();
}

uint64 UVBAirAfterimageComponent::BeginSampling(const UVBAirAfterimageConfig* InConfig)
{
	StopSampling(ActiveRequestId);
	if (!IsRegistered() || !GetOwner() || !GetWorld() || GetOwner()->GetNetMode() == NM_DedicatedServer)
	{
		ResetVisuals();
		return 0;
	}

	if (!InConfig || !InConfig->GhostMaterial)
	{
		ResetVisuals();
		return 0;
	}

	const TArray<FText> Problems = InConfig->FindProblems();
	if (!Problems.IsEmpty())
	{
		for (const FText& Problem : Problems)
		{
			VB_LOG(Error, "Afterimage %s: %s", *GetNameSafe(InConfig), *Problem.ToString());
		}

		ResetVisuals();
		return 0;
	}

	TArray<FSource> NewSources;
	FString Error;
	if (!GatherSources(NewSources, Error) || NewSources.Num() > InConfig->MaxPartsPerGhost)
	{
		VB_LOG(Error, "Afterimage source rejected: %s (parts=%d, limit=%d)", *Error, NewSources.Num(), InConfig->MaxPartsPerGhost);
		ResetVisuals();
		return 0;
	}

	if (Config.Get() != InConfig || Sources != NewSources || PoolMaterial.Get() != InConfig->GhostMaterial.Get()
		|| Ghosts.Num() > InConfig->MaxGhosts)
	{
		ClearSnapshots();
	}

	Config = InConfig;
	PoolMaterial = InConfig->GhostMaterial;
	PoolCapacity = InConfig->MaxGhosts;
	Sources = MoveTemp(NewSources);
	BindAppearance();
	ActiveRequestId = ++NextRequestId;
	if (ActiveRequestId == 0)
	{
		ActiveRequestId = ++NextRequestId;
	}

	SinceSample = 0.0f;
	LastSampleLocation = GetOwner()->GetActorLocation();
	AcquireMeshUpdates();
	SetComponentTickEnabled(true);
	return ActiveRequestId;
}

void UVBAirAfterimageComponent::StopSampling(uint64 RequestId)
{
	if (RequestId == 0 || RequestId != ActiveRequestId)
	{
		return;
	}

	ActiveRequestId = 0;
	ReleaseMeshUpdates();
	if (GetLiveGhostCount() == 0)
	{
		SetComponentTickEnabled(false);
	}
}

int32 UVBAirAfterimageComponent::GetLiveGhostCount() const
{
	int32 Count = 0;
	for (const FVBAirAfterimageSnapshot& Ghost : Ghosts)
	{
		Count += Ghost.Age >= 0.0f ? 1 : 0;
	}

	return Count;
}

FVBAirAfterimageSnapshot* UVBAirAfterimageComponent::AcquireSnapshot()
{
	for (FVBAirAfterimageSnapshot& Ghost : Ghosts)
	{
		if (Ghost.Age < 0.0f)
		{
			return &Ghost;
		}
	}

	if (Ghosts.Num() < PoolCapacity)
	{
		FVBAirAfterimageSnapshot& Ghost = Ghosts.AddDefaulted_GetRef();
		Ghost.Material = UMaterialInstanceDynamic::Create(Config->GhostMaterial, this);
		if (!Ghost.Material)
		{
			Ghosts.Pop();
			VB_LOG(Error, "Afterimage material instance creation failed");
			return nullptr;
		}

		for (const FSource& Source : Sources)
		{
			UPoseableMeshComponent* Mesh = NewObject<UPoseableMeshComponent>(GetOwner(), NAME_None, RF_Transient);
			Ghost.Meshes.Add(Mesh);
			Mesh->SetVisibility(false);
			Mesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Mesh->SetCastShadow(false);
			Mesh->SetCanEverAffectNavigation(false);
			Mesh->SetUsingAbsoluteLocation(true);
			Mesh->SetUsingAbsoluteRotation(true);
			Mesh->SetUsingAbsoluteScale(true);
			Mesh->SetSkinnedAssetAndUpdate(Source.Asset.Get());
			GetOwner()->AddInstanceComponent(Mesh);
			Mesh->RegisterComponent();
			Mesh->SetComponentTickEnabled(false);
			for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
			{
				Mesh->SetMaterial(Slot, Ghost.Material);
			}
		}

		return &Ghost;
	}

	if (Ghosts.IsEmpty())
	{
		VB_LOG(Error, "Afterimage has no validated pool capacity");
		return nullptr;
	}

	FVBAirAfterimageSnapshot* Oldest = &Ghosts[0];
	for (FVBAirAfterimageSnapshot& Ghost : Ghosts)
	{
		if (Ghost.Age > Oldest->Age)
		{
			Oldest = &Ghost;
		}
	}

	return Oldest;
}

void UVBAirAfterimageComponent::Snapshot()
{
	for (const FSource& Source : Sources)
	{
		if (Source.Pose->GetBoneSpaceTransformsView().IsEmpty())
		{
			return;
		}
	}

	FVBAirAfterimageSnapshot* Ghost = AcquireSnapshot();
	if (!Ghost)
	{
		ResetVisuals();
		return;
	}

	for (int32 Part = 0; Part < Sources.Num(); ++Part)
	{
		USkeletalMeshComponent* Visual = Sources[Part].Visual.Get();
		UPoseableMeshComponent* Mesh = Ghost->Meshes[Part];
		Mesh->SetWorldTransform(Visual->GetComponentTransform());
		Mesh->SetForcedLOD(Visual->GetPredictedLODLevel() + 1);
		Mesh->CopyPoseFromSkeletalComponent(Sources[Part].Pose.Get());
		const FSkeletalMeshRenderData* RenderData = Sources[Part].Asset->GetResourceForRendering();
		if (RenderData)
		{
			for (int32 LOD = 0; LOD < RenderData->LODRenderData.Num(); ++LOD)
			{
				Mesh->ShowAllMaterialSections(LOD);
				const TArray<FSkelMeshRenderSection>& Sections = RenderData->LODRenderData[LOD].RenderSections;
				for (int32 Section = 0; Section < Sections.Num(); ++Section)
				{
					Mesh->ShowMaterialSection(Sections[Section].MaterialIndex, Section,
						Visual->IsMaterialSectionShown(Sections[Section].MaterialIndex, LOD), LOD);
				}
			}
		}

		Mesh->RefreshBoneTransforms();
		Mesh->SetVisibility(true);
	}

	Ghost->Material->SetVectorParameterValue(UVBAirAfterimageConfig::TintParameter, Config->Tint);
	Ghost->Material->SetScalarParameterValue(UVBAirAfterimageConfig::FresnelParameter, Config->FresnelStrength);
	Ghost->Material->SetScalarParameterValue(UVBAirAfterimageConfig::OpacityParameter, Config->OpacityAt(0.0f));
	Ghost->Age = 0.0f;
	SinceSample = 0.0f;
	LastSampleLocation = GetOwner()->GetActorLocation();
}

void UVBAirAfterimageComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	if (Config && (!FMath::IsFinite(Config->GhostLifetime) || Config->GhostLifetime <= 0.0f))
	{
		VB_LOG(Error, "Afterimage lifetime became invalid during playback");
	}

	if (!Config || !FMath::IsFinite(Config->GhostLifetime) || Config->GhostLifetime <= 0.0f || (IsSampling() && !SourcesRemainValid()))
	{
		ResetVisuals();
		return;
	}

	if (!FMath::IsFinite(DeltaTime) || DeltaTime <= 0.0f)
	{
		return;
	}

	for (FVBAirAfterimageSnapshot& Ghost : Ghosts)
	{
		if (Ghost.Age < 0.0f)
		{
			continue;
		}

		Ghost.Age += DeltaTime;
		if (Ghost.Age >= Config->GhostLifetime)
		{
			Ghost.Age = -1.0f;
			for (UPoseableMeshComponent* Mesh : Ghost.Meshes)
			{
				Mesh->SetVisibility(false);
			}
		}
		else
		{
			Ghost.Material->SetScalarParameterValue(UVBAirAfterimageConfig::OpacityParameter, Config->OpacityAt(Ghost.Age));
		}
	}

	if (IsSampling())
	{
		SinceSample += DeltaTime;
		if (SinceSample >= Config->SampleInterval
			&& FVector::DistSquared(GetOwner()->GetActorLocation(),
				LastSampleLocation) >= FMath::Square(Config->MinSampleDistance))
		{
			Snapshot();
		}
	}

	if (!IsSampling() && GetLiveGhostCount() == 0)
	{
		SetComponentTickEnabled(false);
	}
}

void UVBAirAfterimageComponent::ClearSnapshots()
{
	for (FVBAirAfterimageSnapshot& Ghost : Ghosts)
	{
		for (UPoseableMeshComponent* Mesh : Ghost.Meshes)
		{
			if (!IsValid(Mesh))
			{
				continue;
			}

			if (AActor* Owner = GetOwner())
			{
				Owner->RemoveInstanceComponent(Mesh);
			}

			Mesh->DestroyComponent();
		}
	}

	Ghosts.Reset();
}

void UVBAirAfterimageComponent::ResetVisuals()
{
	StopSampling(ActiveRequestId);
	ClearSnapshots();
	Sources.Reset();
	Config = nullptr;
	PoolMaterial.Reset();
	PoolCapacity = 0;
	SetComponentTickEnabled(false);
}

void UVBAirAfterimageComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	ResetVisuals();
	UnbindAppearance();
	Super::EndPlay(EndPlayReason);
}

void UVBAirAfterimageComponent::OnUnregister()
{
	ResetVisuals();
	UnbindAppearance();
	Super::OnUnregister();
}
