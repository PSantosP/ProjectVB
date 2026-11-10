// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBAbilityFailureMessageData.generated.h"

USTRUCT(BlueprintType)
struct VB_API FVBAbilityFailureMessageRow
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ability|Failure")
	FGameplayTag FailureTag;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ability|Failure")
	FText Message;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ability|Failure")
	int32 Priority = 0;
	// 플레이어에게 알리지 않는 이유. Message 는 비워 둔다 - 메시지가 있는 Silent 는 모호해서 거절한다.
	//   Silent 줄은 결정이고, 줄이 없으면 여전히 검사에 실패한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ability|Failure")
	bool bSilent = false;
};

UCLASS(BlueprintType)
class VB_API UVBAbilityFailureMessageData : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Ability|Failure")
	TArray<FVBAbilityFailureMessageRow> Rows;

	// 정확히 맞는 이유만 본다 - 우선순위가 가장 높은 줄 하나, 같으면 태그 사전순, 없으면 Unknown. 데이터가 틀리면 null.
	//   줄이 Silent 일 수 있다 - 그러면 호출한 쪽이 아무것도 보이지 않는다.
	const FVBAbilityFailureMessageRow* FindRow(const FGameplayTagContainer& FailureTags) const;

	// 줄이 필요한 이유들 - Ability.Fail 아래 등록된 모든 leaf 태그(더 깊은 이유의 부모는 이유가 아니다).
	static FGameplayTagContainer RequiredReasons();

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
