// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "VBItemSettings.generated.h"

class AVBWorldItem;

/** 아이템이 세계에 놓일 때의 전역 설정(Project Settings > Game > VB Items). 넘친 몫 떨구기가 쓴다. */
UCLASS(config=Game, defaultconfig, meta=(DisplayName="VB Items"))
class VB_API UVBItemSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	// 떨굴 때 만드는 줍기 액터(BP_WorldItem). 비면 넘친 몫을 버리고 경고한다.
	UPROPERTY(config, EditAnywhere, Category="VB|Item")
	TSoftClassPtr<AVBWorldItem> WorldItemClass;

	// 한 자리에 여럿을 떨굴 때 벌리는 반지름(cm). 첫 줄은 가운데에 놓는다.
	UPROPERTY(config, EditAnywhere, Category="VB|Item", meta=(ClampMin="0"))
	float DropSpreadRadius = 40.0f;

	// 바닥을 찾는 아래 방향 추적 길이(cm). 못 찾으면 그 위치에 놓고 경고한다.
	UPROPERTY(config, EditAnywhere, Category="VB|Item", meta=(ClampMin="0"))
	float DropGroundTraceDistance = 500.0f;
};
