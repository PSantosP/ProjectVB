#pragma once

#include "PlayBot/VBPlayBotScenarioBase.h"

class FVBPlayBotPlayerAction;

// ini 가 오래 쓸 플레이어 행동 하나를 고른다. GuardParry/Execution/Ladder 는 따로 된 시나리오로 남는다
class FVBPlayBotScenario_PlayerActions : public FVBPlayBotScenarioBase
{
public:
	virtual ~FVBPlayBotScenario_PlayerActions() override;
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& Error) override;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) override;
	virtual void Teardown(FVBPlayBotContext& Ctx) override;

private:
	TUniquePtr<FVBPlayBotPlayerAction> Action;
};
