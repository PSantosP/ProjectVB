// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "VBAirAfterimageConfig.generated.h"

class UMaterialInterface;
class UVBCombatFeedbackProfile;
class UVBBodyFadeConfig;

/** 추격 잔상의 수명과 예산, 재질 및 화면 효과 설정. */
UCLASS(BlueprintType)
class VB_API UVBAirAfterimageConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	static const FName OpacityParameter;
	static const FName TintParameter;
	static const FName FresnelParameter;
	static constexpr int32 MaxSnapshotMeshes = 32;

	// 비우면 캐릭터 잔상 없이 화면 효과만 사용한다.
	UPROPERTY(EditAnywhere, Category="VB|Afterimage")
	TObjectPtr<UMaterialInterface> GhostMaterial;

	// 한 자세의 바디와 의상 메시를 합친 잔상 묶음의 최대 수.
	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="1", ClampMax="8"))
	int32 MaxGhosts = 3;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="1", ClampMax="16"))
	int32 MaxPartsPerGhost = 8;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="0.02"))
	float GhostLifetime = 0.22f;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="0.01"))
	float SampleInterval = 0.03f;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="0.1"))
	float MinSampleDistance = 20.0f;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="0.0", ClampMax="1.0"))
	float StartOpacity = 0.30f;

	// 정규화된 수명에 따른 배율. 비우면 선형으로 사라진다.
	UPROPERTY(EditAnywhere, Category="VB|Afterimage")
	FRuntimeFloatCurve OpacityCurve;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage")
	FLinearColor Tint = FLinearColor(0.55f, 0.75f, 1.0f);

	UPROPERTY(EditAnywhere, Category="VB|Afterimage", meta=(ClampMin="0.0"))
	float FresnelStrength = 1.0f;

	// 시간 배율과 월드 효과가 없는 화면 전용 프로필.
	UPROPERTY(EditAnywhere, Category="VB|Afterimage")
	TObjectPtr<const UVBCombatFeedbackProfile> WhooshProfile;

	UPROPERTY(EditAnywhere, Category="VB|Afterimage")
	TObjectPtr<UVBBodyFadeConfig> BodyFade;

	float OpacityAt(float Age) const;
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
