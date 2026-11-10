// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBRegionDefinition.generated.h"

class FDataValidationContext;

/**
 * 지역 하나의 정의(자산 ID = RegionId). 이름과 열림, 방문을 뜻하는 사실만 든다 - 저장은 사실이 한다.
 * 진입은 영역 트리거에 꽂은 FVBEffect_EnterRegion 이 알린다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBRegionDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetRegionAssetType();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Map.Region"))
	FGameplayTag RegionId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress")
	FText DisplayName;

	// 비면 처음부터 열려 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Story"))
	FGameplayTag UnlockFact;

	// 들어간 적이 있는가. 비면 매번 처음으로 본다(큰 배너). 세계 사실이라 협동에서는 모두에게 공유된다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Story"))
	FGameplayTag VisitedFact;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress")
	int32 SortOrder = 0;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
