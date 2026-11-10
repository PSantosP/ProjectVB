// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"

// 소품, 설정의 ValidateSetup 이 낸 문장 하나(빈 문자열 = 문제 없음)를 데이터 검사 결과로 올린다.
//   Owner 는 같은 문장을 시작 때 로그로도 남긴다 - 검사와 실행이 한 판단을 쓰도록 문장은 Owner 한 곳에서 만든다.
inline EDataValidationResult VBReportSetupProblem(FDataValidationContext& Context, const UObject& Owner, const FString& Problem,
	EDataValidationResult Result)
{
	if (Problem.IsEmpty())
	{
		return Result;
	}

	Context.AddError(FText::FromString(FString::Printf(TEXT("%s: %s"), *Owner.GetName(), *Problem)));
	return EDataValidationResult::Invalid;
}
#endif
