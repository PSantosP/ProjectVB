// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "GameModes/VBGameState.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "Narrative/Campaign/VBCampaignStateComponent.h"

AVBGameState::AVBGameState()
{
	bReplicates = true;
	StoryState = CreateDefaultSubobject<UVBStoryStateComponent>(TEXT("StoryState"));
	DifficultyState = CreateDefaultSubobject<UVBDifficultyStateComponent>(TEXT("DifficultyState"));
	CampaignState = CreateDefaultSubobject<UVBCampaignStateComponent>(TEXT("CampaignState"));
}
