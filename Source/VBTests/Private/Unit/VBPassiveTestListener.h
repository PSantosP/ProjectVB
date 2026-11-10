// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBPassiveTestListener.generated.h"

/** 실제 Blueprint 바인딩 경로의 복원 여부를 기록하는 시험 수신자다. */
UCLASS()
class UVBPassiveTestListener : public UObject
{
	GENERATED_BODY()

public:
	TArray<bool> RestoredSignals;

	UFUNCTION()
	void Heard(bool bRestored) { RestoredSignals.Add(bRestored); }
};
