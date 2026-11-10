// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBSpeakerDefinition.generated.h"

class UTexture2D;

/**
 * 말하는 사람 하나. 대사 노드는 화자를 태그로만 들어 이름, 초상화가 바뀌어도 대사 자산을 고치지 않는다.
 */
UCLASS(BlueprintType, Const)
class VB_API UVBSpeakerDefinition : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue", meta=(Categories="Speaker"))
	FGameplayTag SpeakerId;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	FText DisplayName;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Dialogue")
	TSoftObjectPtr<UTexture2D> Portrait;
};
