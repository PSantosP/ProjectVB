// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBTraversalSettings.generated.h"

class UMaterialInterface;
class UStaticMesh;

/**
 * 넘기 블록의 프로젝트 전역 표시값(Project Settings > Game > VB Traversal).
 * 넘을 수 있는 블록만 같은 페인트 띠를 갖게 해서, 높이만 맞는 일반 물건과 눈으로 구별되게 한다.
 */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Traversal"))
class VB_API UVBTraversalSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 모서리 띠 재질. 비우면 프로젝트 전체에서 띠를 그리지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Traversal|LedgePaint")
	TSoftObjectPtr<UMaterialInterface> LedgePaintMaterial;

	// 띠 메시. 한 변 100cm 인 원점 중심 상자를 늘려 쓴다(엔진 Cube 와 같은 규격).
	UPROPERTY(config, EditAnywhere, Category="VB|Traversal|LedgePaint")
	TSoftObjectPtr<UStaticMesh> LedgePaintMesh;

	// 윗면 모서리 띠 폭(cm).
	UPROPERTY(config, EditAnywhere, Category="VB|Traversal|LedgePaint", meta=(ClampMin="1"))
	float LedgePaintWidth = 6.0f;

	// 옆면으로 내려오는 띠 높이(cm). 0 이면 옆면 띠를 그리지 않는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Traversal|LedgePaint", meta=(ClampMin="0"))
	float LedgePaintFaceHeight = 5.0f;
};
