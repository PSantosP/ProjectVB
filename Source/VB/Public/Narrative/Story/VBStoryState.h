// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "VBStoryState.generated.h"

/** 이름 붙은 숫자 하나(예: Story.Count.PatientsSaved). 배열 원소라 복제된다 - TMap 은 복제되지 않는다. */
USTRUCT(BlueprintType)
struct FVBStoryCounter
{
	GENERATED_BODY()

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Story")
	FGameplayTag Tag;

	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Story")
	int32 Value = 0;
};

/**
 * 세계에 대해 참인 것들 - 켜진 사실 태그와 이름 붙은 카운터.
 * 월드 없이 조건, 저장을 시험하려고 데이터와 연산만 둔다. 복제, 권한은 UVBStoryStateComponent 가 맡는다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBStoryState
{
	GENERATED_BODY()

	// 켜진 사실만 담는다. 꺼짐 = 없음.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Story")
	FGameplayTagContainer Facts;

	// 0 인 카운터는 담지 않는다 - 없는 것과 0 이 같은 뜻이라 두 가지 표현이 생기지 않게.
	UPROPERTY(SaveGame, BlueprintReadOnly, Category="VB|Story")
	TArray<FVBStoryCounter> Counters;

	// 정확히 이 태그가 켜져 있나. 부모 태그로 묻지 않는다 - Story.Village 가 켜졌다고 Story.Village.WellCleansed 가 참이 되면 안 된다.
	bool HasFact(FGameplayTag Fact) const;

	// 바뀌었으면 true(같은 값이면 false - 바뀜 신호를 헛되이 보내지 않게).
	bool SetFact(FGameplayTag Fact, bool bOn);

	int32 GetCounter(FGameplayTag Counter) const;

	// 더하고 결과를 돌려준다. 결과가 0 이면 항목을 지운다.
	int32 AddToCounter(FGameplayTag Counter, int32 Delta);
	// 그 값으로 정한다. 바뀌었으면 true(같은 값이면 false). 0 은 항목을 지운다 - 없는 카운터에 0 은 바뀜 없음.
	bool SetCounter(FGameplayTag Counter, int32 Value);

	bool operator==(const FVBStoryState& Other) const;
};
