// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "VBSavePoint.generated.h"

class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 저장 포인트 - 범위에 들어오면 입력 없이 한 번 저장한다(설정 Mode 는 Auto). 알림은 HUD 가 저장 완료를 듣고 띄운다.
 * 빠른 이동 지점이기도 하다: 지점 ID 가 있으면 UVBTravelPointSubsystem 에 등록되고 처음 저장 때 발견된다.
 * 목적지 목록이 늘 떠 있도록 공간 로딩을 끈다.
 */
UCLASS()
class VB_API AVBSavePoint : public AActor
{
	GENERATED_BODY()

public:
	AVBSavePoint();

	const FGameplayTag& GetPointId() const { return PointId; }
	const FGameplayTag& GetRegionId() const { return RegionId; }
	const FText& GetDisplayName() const { return DisplayName; }
	// 도착 자리 = ArrivalPoint 의 월드 변환(저장점 앞, 땅 위에 둔다).
	FTransform GetArrivalTransform() const;
	// 이동 상호작용의 범위(설정 자산). 빠른 이동이 저장점 곁인지 판정할 때 쓴다.
	float GetTravelInteractRange() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 빠른 이동 지점 ID. 비면 저장만 하고 목적지가 되지 않는다. 저장점마다 하나(겹치면 뒤의 것이 등록되지 않는다).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Travel", meta=(Categories="Map.TravelPoint"))
	FGameplayTag PointId;

	// 목적지 목록을 묶는 지역(UVBRegionDefinition::RegionId).
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Travel", meta=(Categories="Map.Region"))
	FGameplayTag RegionId;

	// 목적지 목록에 보일 이름.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Travel")
	FText DisplayName;

	// 도착 자리. 배치에서 저장점 앞 땅 위로 옮긴다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Travel")
	TObjectPtr<USceneComponent> ArrivalPoint;

	// 빠른 이동을 여는 두 번째 상호작용(누름). 저장(자동)과 따로 있다. 지점 ID 가 쓸 수 없으면 꺼 둔다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Travel")
	TObjectPtr<UVBInteractableComponent> TravelInteractable;

	// 보이는 마커 메시. 메시는 배치에서 정한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UStaticMeshComponent> Marker;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	TObjectPtr<UVBInteractableComponent> Interactable;

private:
	friend class FVBTravelPointTestAccess;

	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void HandleTravelInteracted(AActor* InteractingActor);
};
