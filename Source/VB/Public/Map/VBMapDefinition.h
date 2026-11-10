// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBMapDefinition.generated.h"

class UTexture2D;

/**
 * 맵 하나의 미리 구운 지도 그림과 그 그림이 덮는 월드 범위. 미니맵, 지역 지도가 같이 읽는다.
 * 그림과 범위는 굽는 도구가 함께 쓴다 - 범위만 손으로 고치면 어긋난다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBMapDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Map")
	TObjectPtr<UTexture2D> Texture;

	// 그림의 왼쪽 아래 모서리가 아니라 월드 XY 의 작은 쪽(최소 X, 최소 Y). 그림의 아래 = 최소 X, 왼쪽 = 최소 Y.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Map")
	FVector2D WorldMin = FVector2D::ZeroVector;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Map")
	FVector2D WorldMax = FVector2D::ZeroVector;

	// 쓸 수 있나: 그림이 있고 범위가 맞다. 아니면 미니맵, 지도가 숨는다.
	bool IsUsable() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
