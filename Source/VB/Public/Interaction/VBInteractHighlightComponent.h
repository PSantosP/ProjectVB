// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBInteractHighlightComponent.generated.h"

class UPrimitiveComponent;
class UVBInteractableComponent;
class UVBInteractorComponent;

/**
 * 지금 누를 수 있는 대상(UVBInteractorComponent 포커스)에 커스텀 뎁스 스텐실을 찍는다. 그리는 쪽은 후처리 외곽선 재질이다.
 * 플레이어가 조종하는 로컬 폰만 찍는다. 지울 때는 끔이 아니라 찍기 전 값으로 되돌린다(다른 표시의 커스텀 뎁스를 지키려고).
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBInteractHighlightComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBInteractHighlightComponent();

	// 대상 액터의 보이는 기본 요소에 번호를 찍는다(nullptr 이면 지우기만). 포커스 신호가 부르고, 시험이 직접 부른다.
	void SetHighlightedActor(AActor* NewActor);
	AActor* GetHighlightedActor() const { return HighlightedActor.Get(); }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 처형 창(UVBGC_FinisherWindow::OutlineStencilValue = 1)과 겹치면 안 된다 - 머티리얼이 번호로 색을 가른다. 0 이면 안 찍는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Interaction", meta=(ClampMin="0", ClampMax="255"))
	int32 OutlineStencilValue = 2;

private:
	UFUNCTION()
	void HandleFocusChanged(UVBInteractableComponent* NewFocus);

	void RestoreStamped();

	// 찍기 전 상태. 되돌릴 때 이 값으로 돌려놓는다.
	struct FStampedPrimitive
	{
		TWeakObjectPtr<UPrimitiveComponent> Primitive;
		bool bHadCustomDepth = false;
		int32 PreviousStencil = 0;
	};
	TArray<FStampedPrimitive> Stamped;
	TWeakObjectPtr<AActor> HighlightedActor;
	TWeakObjectPtr<UVBInteractorComponent> BoundInteractor;
};
