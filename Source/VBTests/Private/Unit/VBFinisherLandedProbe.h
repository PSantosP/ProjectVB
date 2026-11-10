// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBFinisherLandedProbe.generated.h"

// UVBFinisherComponent::OnInteractionVictimLanded 방송 횟수를 센다 - dynamic delegate 라 시험이 묶을 UFUNCTION 이 필요하다.
UCLASS()
class UVBFinisherLandedProbe : public UObject
{
	GENERATED_BODY()

public:
	int32 Count = 0;

	UFUNCTION()
	void OnLanded(FVector LaunchVelocity) { ++Count; }
};
