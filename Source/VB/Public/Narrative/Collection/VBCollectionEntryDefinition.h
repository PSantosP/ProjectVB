#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBCollectionEntryDefinition.generated.h"

class UTexture2D;
class FDataValidationContext;
class UVBItemDefinition;
struct FVBStoryState;

/**
 * 도감 항목 하나의 정의(자산 ID = DiscoveryFact). 내용만 들고, 열렸는지는 사실 저장소가 답한다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBCollectionEntryDefinition : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static FPrimaryAssetType GetCollectionAssetType();
	static FPrimaryAssetId MakeCollectionAssetId(FGameplayTag Fact);
	static const UVBCollectionEntryDefinition* FindById(FGameplayTag Fact);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection", meta=(Categories="Story.Collection"))
	FGameplayTag DiscoveryFact;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection")
	FText Title;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection", meta=(MultiLine=true))
	FText Body;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection")
	TSoftObjectPtr<UTexture2D> Icon;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection")
	TSoftObjectPtr<UTexture2D> Illustration;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Collection")
	int32 SortOrder = 0;

	virtual FPrimaryAssetId GetPrimaryAssetId() const override;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};

namespace VBCollection
{
	VB_API FGameplayTag GetCategory(FGameplayTag Fact);
	VB_API bool IsEntryFact(FGameplayTag Fact);
	VB_API FGameplayTag GetReadingFact(const UVBItemDefinition* Item);
	VB_API TArray<const UVBCollectionEntryDefinition*> BuildRows(
		const TArray<const UVBCollectionEntryDefinition*>& Definitions, const FVBStoryState& Story, FGameplayTag Filter);
#if WITH_EDITOR
	VB_API bool ValidateItemReferences(const UVBItemDefinition& Item, FDataValidationContext& Context);
#endif
}
