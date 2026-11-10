// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBActivationVisualProfile.generated.h"

// 켜짐 상태에 따라 바꿀 스칼라 머티리얼 파라미터 하나(예: 발광 세기 꺼짐 0 / 켜짐 20).
USTRUCT(BlueprintType)
struct FVBActivationScalarParam
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FName Parameter;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	float Off = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	float On = 1.0f;
};

// 켜짐 상태에 따라 바꿀 색 머티리얼 파라미터 하나(예: 출구 유도등 꺼짐 검정 / 켜짐 초록).
USTRUCT(BlueprintType)
struct FVBActivationColorParam
{
	GENERATED_BODY()

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FName Parameter;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FLinearColor Off = FLinearColor::Black;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	FLinearColor On = FLinearColor::White;
};

// 켜짐 상태 하나에서 그릴 값. 순서는 프로필 배열과 같다(ScalarValues[i] = ScalarParams[i] 의 값).
struct FVBActivationVisualValues
{
	TArray<float> ScalarValues;
	TArray<FLinearColor> ColorValues;
	bool bLightsVisible = false;
};

/**
 * 켜짐 상태(UVBInteractableComponent::bActivated)를 따르는 외형 - 머티리얼 파라미터와 조명 켜고 끄기. 기구 종류마다 하나.
 * 상태는 없고 UVBActivationVisualComponent 가 Resolve 를 부른다. 꺼진 조명은 숨겨 그림자 비용도 내지 않는다.
 */
UCLASS(BlueprintType)
class VB_API UVBActivationVisualProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	TArray<FVBActivationScalarParam> ScalarParams;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	TArray<FVBActivationColorParam> ColorParams;

	// 참이면 켜졌을 때 조명이 보인다. 거짓이면 반대 - 전력이 다른 회로로 가면 꺼지는 등.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Interaction")
	bool bLightsOnWhenActivated = true;

	// 켜짐 상태 하나에 대한 값. 월드 없이 시험한다.
	FVBActivationVisualValues Resolve(bool bActivated) const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
