#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBBodyFadeConfig.generated.h"

class UMaterial;
class UMaterialInterface;

USTRUCT(BlueprintType)
struct FVBBodyFadeMaterial
{
	GENERATED_BODY()
	UPROPERTY(EditAnywhere, Category="VB|BodyFade")
	TObjectPtr<UMaterial> Source;
	UPROPERTY(EditAnywhere, Category="VB|BodyFade")
	TObjectPtr<UMaterialInterface> Faded;
};

/** 바디 머티리얼의 호환 쌍과 추격 중 표시 비율을 지정한다. */
UCLASS(BlueprintType)
class VB_API UVBBodyFadeConfig : public UDataAsset
{
	GENERATED_BODY()
public:
	static const FName CoverageParameter;
	UPROPERTY(EditAnywhere, Category="VB|BodyFade")
	TArray<FVBBodyFadeMaterial> Materials;
	UPROPERTY(EditAnywhere, Category="VB|BodyFade", meta=(ClampMin="0.0", ClampMax="1.0"))
	float MinimumCoverage = 0.65f;
	UPROPERTY(EditAnywhere, Category="VB|BodyFade", meta=(ClampMin="0.01", ClampMax="0.99"))
	float FadeInFraction = 0.25f;
	float CoverageAt(float Progress) const;
	UMaterialInterface* FindVariant(const UMaterialInterface* Original) const;
	TArray<FText> FindProblems() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
