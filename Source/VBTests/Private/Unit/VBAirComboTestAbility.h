// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "AbilitySystem/Abilities/VBGA_AirCombo.h"
#include "VBAirComboTestAbility.generated.h"

// UVBGA_AirCombo 는 abstract 라(실제 것은 BP) 아무것도 더하지 않은 구체 fixture 를 둔다.
UCLASS()
class UVBAirComboTestAbility : public UVBGA_AirCombo
{
	GENERATED_BODY()
};
