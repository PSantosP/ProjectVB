// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Narrative/Progress/VBChapterDefinition.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#endif

FPrimaryAssetType UVBChapterDefinition::GetChapterAssetType()
{
	static const FPrimaryAssetType Type(TEXT("VBChapter"));
	return Type;
}

FPrimaryAssetId UVBChapterDefinition::GetPrimaryAssetId() const
{
	return FPrimaryAssetId(GetChapterAssetType(), ChapterId.IsValid() ? ChapterId.GetTagName() : GetFName());
}

#if WITH_EDITOR
EDataValidationResult UVBChapterDefinition::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	auto Error = [&](const TCHAR* Message)
	{
		Context.AddError(FText::FromString(Message));
		Result = EDataValidationResult::Invalid;
	};
	if (!ChapterId.IsValid()) { Error(TEXT("ChapterId is empty.")); }
	if (!ReachedFact.IsValid()) { Error(TEXT("ReachedFact is empty - nothing can ever enter this chapter.")); }
	if (Title.IsEmpty()) { Error(TEXT("Chapter Title is empty.")); }
	return Result;
}
#endif
