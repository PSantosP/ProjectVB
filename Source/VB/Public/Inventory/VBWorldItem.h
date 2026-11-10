// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBWorldItem.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;
class UVBItemDefinition;

/**
 * 땅에 놓인 아이템 - 상호작용으로 줍는다. 주운 상태는 상호작용 켜짐 저장으로 남아 다시 생기지 않는다.
 * 줍기 설정은 BP_WorldItem 의 상호작용 컴포넌트에 둔다 - 맵에는 그 BP 를 놓는다.
 */
UCLASS()
class VB_API AVBWorldItem : public AActor
{
	GENERATED_BODY()

public:
	AVBWorldItem();

	virtual void OnConstruction(const FTransform& Transform) override;

	// 서버에서만. 넘친 몫을 Anchor 발밑에 놓는다(SpreadIndex 0 = 가운데). 못 놓으면 경고하고 nullptr.
	//   놓은 줍기는 저장하지 않는다 - 이어하기 뒤에는 없다.
	static AVBWorldItem* SpawnDrop(const AActor* Anchor, const UVBItemDefinition* InItem, int32 InCount, int32 SpreadIndex);

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Item")
	TObjectPtr<UStaticMeshComponent> Mesh;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Item")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 무엇을 몇 개. 놓인 자리마다 정한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item")
	TObjectPtr<const UVBItemDefinition> Item;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Item", meta=(ClampMin="1"))
	int32 Count = 1;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	// 주운 상태면 숨기고 상호작용을 끈다(시작, 변경 공통).
	void ApplyPickedUp(bool bPickedUp);
};
