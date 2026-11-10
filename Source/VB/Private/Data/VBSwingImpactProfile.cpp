// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBSwingImpactProfile.h"

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#define LOCTEXT_NAMESPACE "VBSwingImpactProfile"
#endif

FVector UVBSwingImpactProfile::GetNormalizedAxis() const
{
	// 영벡터면 "축 미저작" 신호로 그대로 돌려준다 - 호출부(GA)가 큐를 쏘지 않는다. 임의 방향으로 대체하지 않는다.
	return LocalSwingAxis.GetSafeNormal();
}

#if WITH_EDITOR
EDataValidationResult UVBSwingImpactProfile::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 영벡터 축이면 쉐이크가 조용히 사라지므로 저작 시점에 잡는다.
	if (LocalSwingAxis.IsNearlyZero())
	{
		Context.AddError(LOCTEXT("VBSwingImpact_ZeroAxis",
			"LocalSwingAxis 가 영벡터입니다. 정규화 결과가 영벡터가 되어 임팩트 쉐이크가 발동하지 않습니다. "
			"밀고 싶은 방향을 아바타 로컬 기준으로 넣으세요(정면=(1,0,0), 오른쪽=(0,1,0), 아래=(0,0,-1))."));

		Result = EDataValidationResult::Invalid;
	}

	return Result;
}
#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
