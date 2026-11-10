#include "PlayBot/Scenarios/VBPlayBotPlayerActionSteps.h"
#include "PlayBot/Scenarios/VBPlayBotPlayerActionCommon.h"

namespace VBPlayBotPlayerActions
{
TUniquePtr<FVBPlayBotPlayerAction> Create(FName Action)
{
	if (Action == TEXT("MoveJumpLand"))
	{
		return CreateMoveJumpLand();
	}

	if (Action == TEXT("LightCombo"))
	{
		return CreateLightCombo();
	}

	if (Action == TEXT("Dodge"))
	{
		return CreateDodge();
	}

	if (Action == TEXT("TurnInPlace"))
	{
		return CreateTurnInPlace();
	}

	return nullptr;
}
}
