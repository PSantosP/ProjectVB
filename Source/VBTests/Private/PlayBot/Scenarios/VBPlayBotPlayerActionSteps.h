#pragma once

#include "PlayBot/VBPlayBotScenarioBase.h"

// FVBPlayBotPlayerAction - 동작 하나가 자기 순서와 관찰자를 갖고, 수명은 시나리오가 쥔다
class FVBPlayBotPlayerAction
{
public:
	virtual ~FVBPlayBotPlayerAction() = default;
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& Error) = 0;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) = 0;
	virtual void Teardown(FVBPlayBotContext& Ctx) {}
};

namespace VBPlayBotPlayerActions
{
	TUniquePtr<FVBPlayBotPlayerAction> Create(FName Action);
}
