// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBMeleeMemoryBindingTestTypes.generated.h"

class AActor;

// VB.Unit.Melee.SwingTargetBinding 용 - 액터 파괴 신호가 dynamic delegate 라 듣는 쪽이 UObject 의 UFUNCTION 이어야 한다.
UCLASS()
class UVBSwingTargetDeathProbe : public UObject
{
	GENERATED_BODY()

public:
	UFUNCTION()
	void OnTargetDestroyed(AActor* DestroyedActor) { ++DestroyedCalls; }

	int32 DestroyedCalls = 0;
};
