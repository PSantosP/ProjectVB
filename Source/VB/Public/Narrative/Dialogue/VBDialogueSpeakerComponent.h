// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "StructUtils/InstancedStruct.h"
#include "VBDialogueSpeakerComponent.generated.h"

class UVBDialogueAsset;

/**
 * NPC 에 붙여 말을 걸 수 있게 한다. 같은 액터의 UVBInteractableComponent 완료(서버)에 대화를 연다.
 * 여는 조건은 지금 말을 받는지, 대화 진입점은 무슨 말부터 할지를 정한다.
 * 전투 중이면 여는 조건 다음에 거절 문구를 알린다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBDialogueSpeakerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBDialogueSpeakerComponent();

protected:
	virtual void BeginPlay() override;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	TObjectPtr<const UVBDialogueAsset> Dialogue;

	// 전부 참일 때만 말을 받는다. 비면 늘 받는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> OpenConditions;

	// 적이 플레이어를 노리는 동안 말을 걸면 띄우는 거절 문구. 자리마다 말투가 달라 화자에 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	FText CombatRefusedText = NSLOCTEXT("VBDialogue", "CombatRefused", "지금은 그럴 틈이 없다");

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);
};
