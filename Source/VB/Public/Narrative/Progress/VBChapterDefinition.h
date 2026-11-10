// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBChapterDefinition.generated.h"

class FDataValidationContext;

/**
 * 챕터 하나의 정의(자산 ID = ChapterId). 제목, 부제, 순서와 진입을 뜻하는 사실 하나만 든다.
 * 지금 챕터는 저장하지 않고 사실에서 센다(VBChapterProgress::ResolveCurrent). 새 챕터 = 이 자산 하나 더.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBChapterDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetChapterAssetType();

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Progress.Chapter"))
	FGameplayTag ChapterId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress")
	FText Title;

	// 비어도 된다. 제목 카드의 둘째 줄.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress")
	FText Subtitle;

	// 켜진 챕터가 여럿이면 이 값이 큰 것이 지금 챕터다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress")
	int32 SortOrder = 0;

	// 이 사실이 켜지면 이 챕터에 들어온 것이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Progress", meta=(Categories="Story"))
	FGameplayTag ReachedFact;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
