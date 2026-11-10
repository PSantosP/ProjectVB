// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Narrative/Story/VBStoryStateComponent.h"

namespace VBStoryTestWorld
{
	// GameState 를 이야기 저장소를 단 채로 스폰한다(AVBGameState 처럼). 시작된 월드에서는 그 자리에서 월드의 GameState 가 된다.
	inline UVBStoryStateComponent* SpawnStory(UWorld& World)
	{
		FActorSpawnParameters Params;
		Params.bDeferConstruction = true;
		AGameStateBase* GameState = World.SpawnActor<AGameStateBase>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
		if (!GameState)
		{
			return nullptr;
		}

		UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>(GameState);
		GameState->AddInstanceComponent(Story);
		Story->RegisterComponent();
		GameState->FinishSpawning(FTransform::Identity);
		return Story;
	}
}
