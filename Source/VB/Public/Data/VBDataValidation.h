// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

namespace VBDataValidation
{
	// FindProblems 가 낸 문제를 검증 오류로 올린다. 문제가 하나라도 있으면 Invalid, 없으면 Base 를 그대로 돌려준다.
	VB_API EDataValidationResult AddProblems(EDataValidationResult Base, const TArray<FText>& Problems, FDataValidationContext& Context);
}
#endif
