// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Progress/VBRegionDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetType UVBRegionDefinition::GetRegionAssetType()
{
	static const FPrimaryAssetType Type(TEXT("VBRegion"));
	return Type;
}

FPrimaryAssetId UVBRegionDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetRegionAssetType(), RegionId.IsValid() ? RegionId.GetTagName() : GetFName());
}

#if WITH_EDITOR
EDataValidationResult UVBRegionDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!RegionId.IsValid())
	{
		Context.AddError(FText::FromString(TEXT("RegionId is empty.")));
		Result = EDataValidationResult::Invalid;
	}

	if (DisplayName.IsEmpty())
	{
		Context.AddError(FText::FromString(TEXT("Region DisplayName is empty.")));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#endif
