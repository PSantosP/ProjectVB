// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Inventory/VBItemAmount.h"
#include "VBEnhancementConfig.generated.h"

class FDataValidationContext;

// 강화 갈래. 무기는 피해 배율, 방어구, 장신구는 능력치 배율이다.
UENUM(BlueprintType)
enum class EVBEnhanceCategory : uint8
{
	None,
	Weapon,
	Armor,
	Accessory,
};

// 한 단계 올리기. Steps[0] = +1 로 올리는 비용과 +1 의 배율.
USTRUCT(BlueprintType)
struct FVBEnhanceStep
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement")
	TArray<FVBItemAmount> Materials;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement", meta=(ClampMin="0"))
	int32 Gold = 0;

	// 이 단계의 배율. 1 = 강화 전과 같다. 실패는 없다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement", meta=(ClampMin="0.01"))
	float Scale = 1.0f;
};

USTRUCT(BlueprintType)
struct FVBEnhanceTrack
{
	GENERATED_BODY()

	// 길이 = MaxLevel. 짧거나 값이 틀리면 그 갈래는 강화하지 않는다(대체 배율로 비용을 받지 않는다 - IsTrackUsable).
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement")
	TArray<FVBEnhanceStep> Steps;
};

/** 강화 수치(UVBEnhancementSettings 가 가리키는 하나). 갈래마다 단계별 비용과 배율. */
UCLASS(BlueprintType)
class VB_API UVBEnhancementConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement", meta=(ClampMin="1"))
	int32 MaxLevel = 5;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement")
	FVBEnhanceTrack WeaponTrack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement")
	FVBEnhanceTrack ArmorTrack;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Enhancement")
	FVBEnhanceTrack AccessoryTrack;

	// 그 갈래의 트랙. None 이면 nullptr.
	const FVBEnhanceTrack* FindTrack(EVBEnhanceCategory Category) const;

	// 길이가 MaxLevel 이고 배율이 유한, 양수, 줄지 않고, 재료 줄이 아이템과 양수 개수를 가졌는가.
	bool IsTrackUsable(EVBEnhanceCategory Category) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
