// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/SceneComponent.h"
#include "GameplayTagContainer.h"
#include "VBWorldMarkerComponent.generated.h"

/**
 * 월드 마커 하나. 아무 액터에 붙이고 MarkerTag 를 주면 퀘스트 목표, 미니맵이 그 태그로 가리킨다.
 * 시작/끝에 UVBWorldMarkerSubsystem 에 올리고 내린다. 표시 전용(복제 없음).
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBWorldMarkerComponent : public USceneComponent
{
	GENERATED_BODY()

public:
	UVBWorldMarkerComponent();

	const FGameplayTag& GetMarkerTag() const { return MarkerTag; }

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 이 마커를 부르는 이름. 퀘스트 목표의 MarkerTag 와 정확히 같아야 가리킨다(계층 매칭은 하지 않는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Map", meta=(Categories="Marker"))
	FGameplayTag MarkerTag;
};
