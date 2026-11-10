// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBActivationVisualComponent.generated.h"

class ULightComponent;
class UMeshComponent;
class UVBActivationVisualProfile;
class UVBInteractableComponent;

/**
 * 켜짐 상태를 그리는 소비자(표시등, 유도등). 상태는 같은 액터의 UVBInteractableComponent 가 들고 여기는 그리기만 한다.
 * 서버, 클라가 각자 같은 신호로 그린다. 대상 메시, 조명은 BeginPlay 에 한 번 모으므로 그 전에 있어야 한다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBActivationVisualComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBActivationVisualComponent();

	// 따를 상호작용 컴포넌트에 묶였나. 시험, 진단용.
	bool IsBoundToSource() const { return Source.IsValid(); }

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBActivationVisualProfile> Profile;

	// 상호작용 컴포넌트가 둘 이상인 액터에서만 적는다 - 따를 컴포넌트의 이름. 하나뿐이면 비워 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FName SourceInteractableName;

	// 비면 이 액터의 조명 전부가 따른다. 적으면 이 컴포넌트 태그를 가진 조명만.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FName LightTag;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	UVBInteractableComponent* FindSource() const;
	void CollectTargets();
	void Apply(bool bActivated);

	struct FMeshTarget
	{
		TWeakObjectPtr<UMeshComponent> Mesh;
		// (슬롯, 프로필 배열 번호). 그 파라미터를 가진 슬롯에만 MID 를 만든다 - 다른 슬롯의 배칭을 깨지 않게.
		TArray<TPair<int32, int32>> ScalarSlots;
		TArray<TPair<int32, int32>> ColorSlots;
	};

	TWeakObjectPtr<UVBInteractableComponent> Source;
	TArray<FMeshTarget> MeshTargets;
	TArray<TWeakObjectPtr<ULightComponent>> LightTargets;
};
