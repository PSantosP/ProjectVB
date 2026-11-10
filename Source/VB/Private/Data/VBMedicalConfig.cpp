// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBMedicalConfig.h"

#if WITH_EDITOR
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBMedicalConfig"

EDataValidationResult UVBMedicalConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	if (!UVBAbilitySystemStatics::IsFinisherActionTag(MedicalActionTag))
	{
		Context.AddError(FText::Format(LOCTEXT("VBMedical_ActionTag",
			"MedicalActionTag({0}) 는 Effect.Execute 또는 Effect.Purify 여야 합니다. "
			"아니면 이 행위의 피해가 «처형 피해» 로 안 읽혀, 처형 창이 켜진 적에게 피해가 0 이 됩니다."),
			FText::FromString(MedicalActionTag.ToString())));
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
