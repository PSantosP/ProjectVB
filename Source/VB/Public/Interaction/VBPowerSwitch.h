// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBPowerSwitch.generated.h"

class AVBPowerPanel;
class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 전력 제어함의 회로 스위치 하나 - 누르면 제어함에 뒤집기를 요청할 뿐 자기 켜짐을 바꾸지 않는다.
 * 켜짐은 제어함이 쓰는 표시 값이고 저장하지 않는다. 거절된 누름이 보상을 내지 않게 완료 효과, 이벤트 태그는 비워 둔다.
 */
UCLASS()
class VB_API AVBPowerSwitch : public AActor
{
	GENERATED_BODY()

public:
	AVBPowerSwitch();

	UVBInteractableComponent* GetInteractable() const { return Interactable; }
	FName GetCircuitId() const { return CircuitId; }
	AVBPowerPanel* GetPanel() const { return Panel; }

protected:
	virtual void BeginPlay() override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Power")
	TObjectPtr<UStaticMeshComponent> Body;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Power")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 이 스위치가 맡은 회로(제어함 Circuits 의 Id).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	FName CircuitId;

	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Power")
	TObjectPtr<AVBPowerPanel> Panel;

private:
	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);
};
