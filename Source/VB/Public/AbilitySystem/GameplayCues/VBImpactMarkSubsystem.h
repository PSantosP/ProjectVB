// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBImpactMarkSubsystem.generated.h"

class USceneComponent;

/**
 * 타격 자국(바닥 핏자국, 상처)의 종류별 개수 상한. 넘으면 가장 오래된 것부터 지운다.
 * 타격 큐는 CDO 라 상태를 못 가져 월드에 둔다. 화면 장식이라 복제하지 않는다.
 */
UCLASS()
class VB_API UVBImpactMarkSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBImpactMarkSubsystem* Get(const UObject* WorldContext);

	// 자국 하나를 맡긴다. 같은 종류가 MaxCount 를 넘으면 가장 오래된 것을 지운다. MaxCount <= 0 이면 세지 않는다.
	void Track(USceneComponent* Mark, FName Kind, int32 MaxCount);

private:
	TMap<FName, TArray<TWeakObjectPtr<USceneComponent>>> Marks;
};
