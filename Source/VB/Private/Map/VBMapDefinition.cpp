// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Map/VBMapDefinition.h"

#include "Engine/Texture2D.h"
#include "Map/VBMapRules.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBMapDefinition"

bool UVBMapDefinition::IsUsable() const
{
	return Texture != nullptr && VBMap::IsValidBounds(WorldMin, WorldMax);
}

#if WITH_EDITOR
EDataValidationResult UVBMapDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!Texture)
	{
		Context.AddError(LOCTEXT("NoTexture", "지도 그림(Texture)이 없다 - 미니맵·지도가 숨는다."));
		Result = EDataValidationResult::Invalid;
	}

	if (!VBMap::IsValidBounds(WorldMin, WorldMax))
	{
		Context.AddError(LOCTEXT("BadBounds", "월드 범위가 틀렸다(WorldMax 가 WorldMin 보다 두 축 모두 커야 한다)."));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif

#undef LOCTEXT_NAMESPACE
