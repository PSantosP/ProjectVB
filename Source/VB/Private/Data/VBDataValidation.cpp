// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBDataValidation.h"

#if WITH_EDITOR
EDataValidationResult VBDataValidation::AddProblems(EDataValidationResult Base, const TArray<FText>& Problems, FDataValidationContext& Context)
{
	for (const FText& Problem : Problems)
	{
		Context.AddError(Problem);
	}

	return Problems.IsEmpty() ? Base : EDataValidationResult::Invalid;
}
#endif
