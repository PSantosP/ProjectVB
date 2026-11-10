// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBActivationVisualComponent.h"
#include "Components/LightComponent.h"
#include "Components/MeshComponent.h"
#include "Data/VBActivationVisualProfile.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBInteractableComponent.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "VB/VB.h"

namespace
{
	// 그 슬롯의 머티리얼이 이 이름, 형의 파라미터를 가졌나. 0, 검정도 값이다 - 돌려준 bool 만 본다.
	bool SlotHasScalar(const UMeshComponent& Mesh, const int32 Slot, const FName Parameter)
	{
		float Value = 0.0f;
		const UMaterialInterface* Material = Mesh.GetMaterial(Slot);
		return Material && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(Parameter), Value);
	}

	bool SlotHasColor(const UMeshComponent& Mesh, const int32 Slot, const FName Parameter)
	{
		FLinearColor Value;
		const UMaterialInterface* Material = Mesh.GetMaterial(Slot);
		return Material && Material->GetVectorParameterValue(FHashedMaterialParameterInfo(Parameter), Value);
	}
}

UVBActivationVisualComponent::UVBActivationVisualComponent()
{
	// 바뀔 때만 그린다 - 매 틱 할 일이 없다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBActivationVisualComponent::BeginPlay()
{
	Super::BeginPlay();

	// 그릴 화면이 없다. HasAuthority 로 거르면 리슨 호스트도 안 그리게 되므로 넷 모드로 거른다.
	if (GetNetMode() == NM_DedicatedServer)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Profile)
	{
		VB_LOG(Warning, "%s: 켜짐 표시 프로필이 비었다 - 아무것도 그리지 않는다", *GetPathNameSafe(this));
		return;
	}
	// 한 액터의 메시, 조명을 그리는 손은 하나다. 둘째부터는 아무것도 안 한다.
	TArray<UVBActivationVisualComponent*> Visuals;
	Owner->GetComponents(Visuals);
	if (Visuals.Num() > 1 && Visuals[0] != this)
	{
		VB_LOG(Warning, "%s: 이 액터에 켜짐 표시가 이미 있다(%s) - 이것은 쓰지 않는다",
			*GetPathNameSafe(this), *GetNameSafe(Visuals[0]));
		return;
	}

	UVBInteractableComponent* Found = FindSource();
	if (!Found)
	{
		return;
	}

	if (GetNetMode() != NM_Standalone && !Owner->GetIsReplicated())
	{
		VB_LOG(Warning, "%s: 소유 액터가 복제되지 않는다 - 클라에는 켜짐 변화가 안 보인다", *GetPathNameSafe(this));
	}

	Source = Found;
	CollectTargets();
	Found->OnActivationChanged.AddUniqueDynamic(this, &UVBActivationVisualComponent::HandleActivationChanged);
	// 지금 값을 바로 그린다. 기본값과 같으면 OnRep 이 오지 않고, 세이브 복원이 이미 끝났을 수도 있다.
	Apply(Found->IsActivated());
}

void UVBActivationVisualComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (UVBInteractableComponent* Bound = Source.Get())
	{
		Bound->OnActivationChanged.RemoveDynamic(this, &UVBActivationVisualComponent::HandleActivationChanged);
	}

	Source.Reset();
	Super::EndPlay(EndPlayReason);
}

void UVBActivationVisualComponent::HandleActivationChanged(const bool bActivated, bool /*bInstant*/)
{
	// 그림은 늘 즉시 바뀐다 - 애니 없이 끝 자세로 맞추라는 bInstant 와 결과가 같다.
	Apply(bActivated);
}

UVBInteractableComponent* UVBActivationVisualComponent::FindSource() const
{
	TArray<UVBInteractableComponent*> Candidates;
	GetOwner()->GetComponents(Candidates);
	if (!SourceInteractableName.IsNone())
	{
		for (UVBInteractableComponent* Candidate : Candidates)
		{
			if (Candidate->GetFName() == SourceInteractableName)
			{
				return Candidate;
			}
		}

		VB_LOG(Warning, "%s: 이름이 %s 인 상호작용 컴포넌트가 없다 - 묶지 않는다",
			*GetPathNameSafe(this), *SourceInteractableName.ToString());
		return nullptr;
	}

	if (Candidates.Num() == 1)
	{
		return Candidates[0];
	}
	// 둘 이상이면 어느 것의 켜짐인지 모른다(여닫기 / 잠그기). 첫 것을 고르면 엉뚱한 상태를 그린다.
	VB_LOG(Warning, "%s: 상호작용 컴포넌트가 %d 개다 - %s 를 적어야 묶는다",
		*GetPathNameSafe(this), Candidates.Num(), TEXT("SourceInteractableName"));
	return nullptr;
}

void UVBActivationVisualComponent::CollectTargets()
{
	MeshTargets.Reset();
	LightTargets.Reset();
	const AActor* Owner = GetOwner();

	TArray<bool> ScalarFound;
	ScalarFound.Init(false, Profile->ScalarParams.Num());
	TArray<bool> ColorFound;
	ColorFound.Init(false, Profile->ColorParams.Num());
	TArray<UMeshComponent*> Meshes;
	Owner->GetComponents(Meshes);
	for (UMeshComponent* Mesh : Meshes)
	{
		FMeshTarget Target;
		Target.Mesh = Mesh;
		for (int32 Slot = 0; Slot < Mesh->GetNumMaterials(); ++Slot)
		{
			for (int32 Index = 0; Index < Profile->ScalarParams.Num(); ++Index)
			{
				if (SlotHasScalar(*Mesh, Slot, Profile->ScalarParams[Index].Parameter))
				{
					Target.ScalarSlots.Emplace(Slot, Index);
					ScalarFound[Index] = true;
				}
			}

			for (int32 Index = 0; Index < Profile->ColorParams.Num(); ++Index)
			{
				if (SlotHasColor(*Mesh, Slot, Profile->ColorParams[Index].Parameter))
				{
					Target.ColorSlots.Emplace(Slot, Index);
					ColorFound[Index] = true;
				}
			}
		}

		if (Target.ScalarSlots.Num() > 0 || Target.ColorSlots.Num() > 0)
		{
			MeshTargets.Add(MoveTemp(Target));
		}
	}
	// 한 슬롯도 그 파라미터를 안 가지면 그 줄은 아무것도 바꾸지 않는다 - 조용하면 '표시등이 왜 안 켜지나' 로만 보인다.
	//   찾았다고 그 파라미터가 발광에 이어졌다는 뜻은 아니다. 그것은 겉모습 확인, PIE 로 본다.
	for (int32 Index = 0; Index < ScalarFound.Num(); ++Index)
	{
		VB_CLOG(!ScalarFound[Index], Warning, "%s: 프로필 %s 의 스칼라 파라미터 %s 를 가진 머티리얼 슬롯이 이 액터에 없다",
			*GetPathNameSafe(this), *GetNameSafe(Profile), *Profile->ScalarParams[Index].Parameter.ToString());
	}

	for (int32 Index = 0; Index < ColorFound.Num(); ++Index)
	{
		VB_CLOG(!ColorFound[Index], Warning, "%s: 프로필 %s 의 색 파라미터 %s 를 가진 머티리얼 슬롯이 이 액터에 없다",
			*GetPathNameSafe(this), *GetNameSafe(Profile), *Profile->ColorParams[Index].Parameter.ToString());
	}

	TArray<ULightComponent*> Lights;
	Owner->GetComponents(Lights);
	for (ULightComponent* Light : Lights)
	{
		if (!LightTag.IsNone() && !Light->ComponentHasTag(LightTag))
		{
			continue;
		}
		// 엔진이 Static 조명의 런타임 변경을 거절한다. 이 프로젝트는 정적 조명을 끄므로 기구 조명은 Movable 로 둔다.
		if (Light->GetMobility() == EComponentMobility::Static)
		{
			VB_LOG(Warning, "%s: 조명 %s 이 Static 이라 켜고 끌 수 없다 - 건너뛴다(Movable 로 둔다)",
				*GetPathNameSafe(this), *Light->GetName());
			continue;
		}

		LightTargets.Add(Light);
	}
}

void UVBActivationVisualComponent::Apply(const bool bActivated)
{
	const FVBActivationVisualValues Values = Profile->Resolve(bActivated);
	for (const FMeshTarget& Target : MeshTargets)
	{
		UMeshComponent* Mesh = Target.Mesh.Get();
		if (!Mesh)
		{
			continue;
		}
		// 슬롯의 머티리얼이 이미 MID 면 그것을 돌려준다 - 토글마다 새로 만들지 않는다.
		for (const TPair<int32, int32>& SlotParam : Target.ScalarSlots)
		{
			if (UMaterialInstanceDynamic* Instance = Mesh->CreateDynamicMaterialInstance(SlotParam.Key))
			{
				Instance->SetScalarParameterValue(Profile->ScalarParams[SlotParam.Value].Parameter, Values.ScalarValues[SlotParam.Value]);
			}
		}

		for (const TPair<int32, int32>& SlotParam : Target.ColorSlots)
		{
			if (UMaterialInstanceDynamic* Instance = Mesh->CreateDynamicMaterialInstance(SlotParam.Key))
			{
				Instance->SetVectorParameterValue(Profile->ColorParams[SlotParam.Value].Parameter, Values.ColorValues[SlotParam.Value]);
			}
		}
	}

	for (const TWeakObjectPtr<ULightComponent>& Weak : LightTargets)
	{
		if (ULightComponent* Light = Weak.Get())
		{
			Light->SetVisibility(Values.bLightsVisible);
		}
	}
}
