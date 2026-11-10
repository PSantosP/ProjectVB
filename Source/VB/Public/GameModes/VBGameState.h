// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "VBGameState.generated.h"

class UVBStoryStateComponent;
class UVBDifficultyStateComponent;
class UVBCampaignStateComponent;

/** VB GameState. 이야기, 난이도, 회차처럼 월드 전체의 상태를 담는 컴포넌트를 소유한다. */
UCLASS()
class VB_API AVBGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	AVBGameState();

protected:
	// 월드 fact, 카운터를 관리한다. 읽기는 UVBStoryStateComponent::Get(월드 컨텍스트) - 이 클래스를 몰라도 된다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Story")
	TObjectPtr<UVBStoryStateComponent> StoryState;

	// 지금 난이도 단계를 관리한다. 읽기는 UVBDifficultyStateComponent::Get(월드 컨텍스트).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Difficulty")
	TObjectPtr<UVBDifficultyStateComponent> DifficultyState;

	// 회차, 앞 회차 결말을 관리한다. 읽기는 UVBCampaignStateComponent::Get(월드 컨텍스트).
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Campaign")
	TObjectPtr<UVBCampaignStateComponent> CampaignState;
};
