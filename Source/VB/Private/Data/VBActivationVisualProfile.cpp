// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBActivationVisualProfile.h"
#include "Misc/DataValidation.h"

FVBActivationVisualValues UVBActivationVisualProfile::Resolve(const bool bActivated) const
{
	FVBActivationVisualValues Values;
	Values.ScalarValues.Reserve(ScalarParams.Num());
	for (const FVBActivationScalarParam& Param : ScalarParams)
	{
		Values.ScalarValues.Add(bActivated ? Param.On : Param.Off);
	}

	Values.ColorValues.Reserve(ColorParams.Num());
	for (const FVBActivationColorParam& Param : ColorParams)
	{
		Values.ColorValues.Add(bActivated ? Param.On : Param.Off);
	}

	Values.bLightsVisible = bActivated == bLightsOnWhenActivated;
	return Values;
}

#if WITH_EDITOR
EDataValidationResult UVBActivationVisualProfile::IsDataValid(FDataValidationContext& Context) const
{
	bool bValid = Super::IsDataValid(Context) != EDataValidationResult::Invalid;
	// 빈 이름은 아무것도 안 바꾸고, 같은 이름이 두 번이면 뒤의 값이 앞의 것을 말없이 덮는다 - 둘 다 저작 사고다.
	TSet<FName> Seen;
	auto CheckName = [&Context, &Seen, &bValid](const FName Parameter, const TCHAR* Kind, const int32 Index)
	{
		if (Parameter.IsNone() || Seen.Contains(Parameter))
		{
			Context.AddError(FText::FromString(FString::Printf(
				TEXT("%s param %d needs a unique, non-empty parameter name."), Kind, Index)));
			bValid = false;
		}

		Seen.Add(Parameter);
	};
	for (int32 Index = 0; Index < ScalarParams.Num(); ++Index)
	{
		CheckName(ScalarParams[Index].Parameter, TEXT("Scalar"), Index);
	}

	for (int32 Index = 0; Index < ColorParams.Num(); ++Index)
	{
		CheckName(ColorParams[Index].Parameter, TEXT("Color"), Index);
	}

	return bValid ? EDataValidationResult::Valid : EDataValidationResult::Invalid;
}
#endif
