#include "PlayBot/Scenarios/VBPlayBotScenario_PlayerActions.h"
#include "PlayBot/Scenarios/VBPlayBotPlayerActionSteps.h"
#include "PlayBot/VBPlayBotScenario.h"

FVBPlayBotScenario_PlayerActions::~FVBPlayBotScenario_PlayerActions() = default;

bool FVBPlayBotScenario_PlayerActions::Setup(FVBPlayBotContext& Ctx, FString& Error)
{
	FString Name;
	if (!Ctx.Config->RequireString(TEXT("Action"), Name, Error))
	{
		return false;
	}

	Action = VBPlayBotPlayerActions::Create(FName(*Name));
	if (!Action)
	{
		Error = FString::Printf(TEXT("unknown player action '%s'"), *Name);
		return false;
	}

	return Action->Setup(Ctx, Error);
}

EVBPlayBotState FVBPlayBotScenario_PlayerActions::Tick(FVBPlayBotContext& Ctx)
{
	return Action->Tick(Ctx);
}

void FVBPlayBotScenario_PlayerActions::Teardown(FVBPlayBotContext& Ctx)
{
	if (Action)
	{
		Action->Teardown(Ctx);
	}

	Action.Reset();
}

VB_PLAYBOT_SCENARIO(FVBPlayBotScenario_PlayerActions, "PlayerActions")
