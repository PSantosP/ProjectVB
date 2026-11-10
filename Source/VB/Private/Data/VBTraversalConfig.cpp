// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBTraversalConfig.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"

#define LOCTEXT_NAMESPACE "VBTraversalConfig"

EDataValidationResult UVBTraversalConfig::IsDataValid(FDataValidationContext& Context) const
{
	TArray<FText> Problems;
	if (!TraversalChooserTable)
	{
		Problems.Add(LOCTEXT("MissingChooser", "TraversalChooserTable 이 비어 있으면 맨틀, 볼트, 허들이 모두 거절됩니다."));
	}
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), Problems, Context);
}

#undef LOCTEXT_NAMESPACE
#endif
