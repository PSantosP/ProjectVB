// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBEndingCatalog.generated.h"

class UVBCinematicDefinition;

// 결말 하나. 고를 수 있는지는 코드가 아니라 이 줄의 조건이 정한다.
USTRUCT(BlueprintType)
struct VB_API FVBEndingDefinition
{
	GENERATED_BODY()

	// 결말 이름표(Ending.*). 저장(앞 회차 결말)과 다음 회차 조건이 이것으로 결말을 가리킨다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending", meta=(Categories="Ending"))
	FGameplayTag Ending;

	// 마지막 선택지에 보이는 말.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending")
	FText ChoiceText;

	// 고르기 전에 플레이어가 듣는 결과(GDD 02 - 숨은 점수로 결말을 정하지 않는다). 비면 검증 오류.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending", meta=(MultiLine=true))
	FText ConsequenceText;

	// 전부 참이면 이 결말을 고를 수 있다. 이번 회차 사실만 본다. 비면 늘 고를 수 있다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> EligibleWhen;

	// 이 결말로 끝낼 때 켜져 있으면 다음 회차가 기억하는 사실(누구를 구했나 등). 나머지 사실은 다음 회차로 가지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending", meta=(Categories="Story"))
	FGameplayTagContainer RememberFacts;

	// 기존 게임 시네마틱 감독의 안전 검사와 완료 영수증을 그대로 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending")
	TObjectPtr<UVBCinematicDefinition> Cinematic;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending")
	FName PlayerBindingTag = TEXT("Player");
};

/** 결말 목록. 프로젝트에 하나(UVBEndingSettings 가 가리킨다). 선택지는 배열 순서대로 보인다. */
UCLASS(BlueprintType)
class VB_API UVBEndingCatalog : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ending", meta=(TitleProperty="Ending"))
	TArray<FVBEndingDefinition> Endings;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
