// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBPlayBotScenario_FastTravel.generated.h"

// 로드 영수증이나 게임 상태를 바꾸지 않고 실제 순간이동 완료 delegate 를 관찰한다.
UCLASS()
class UVBPlayBotTravelObserver : public UObject
{
	GENERATED_BODY()
public:
	int32 Completions = 0;
	UFUNCTION()
	void OnRestored() { ++Completions; }
};
