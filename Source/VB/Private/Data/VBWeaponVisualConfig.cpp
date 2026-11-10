// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBWeaponVisualConfig.h"

#include "NiagaraSystem.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBWeaponVisualConfig"

EVBWeaponShapeSource VBResolveWeaponShapeSource(const FVBWeaponVisualPart& Part)
{
	if (Part.ShapeMeshParameter.IsNone())
	{
		return EVBWeaponShapeSource::None;
	}

	if (Part.Mesh)
	{
		return EVBWeaponShapeSource::WeaponMesh;
	}

	return Part.ShapeBones.IsEmpty() ? EVBWeaponShapeSource::None : EVBWeaponShapeSource::BodyBones;
}

namespace
{
	// 이 이름의 User 파라미터(종류 무관)를 이펙트가 드러내는가.
	bool ExposesUserParameter(const UNiagaraSystem& System, FName Parameter)
	{
		TArray<FNiagaraVariable> Variables;
		System.GetExposedParameters().GetParameters(Variables);
		const FString Wanted = Parameter.ToString();
		return Variables.ContainsByPredicate([&](const FNiagaraVariable& V)
		{
			const FString Name = V.GetName().ToString();
			return Name == Wanted || Name == TEXT("User.") + Wanted;
		});
	}
}

TArray<FText> UVBWeaponVisualConfig::FindProblems() const
{
	TArray<FText> Problems;
	for (const TPair<EVBWeaponType, FVBWeaponVisualStyle>& Pair : Styles)
	{
		for (const FVBWeaponVisualPart& Part : Pair.Value.Parts)
		{
			if (Part.ShapeMeshParameter.IsNone())
			{
				continue;
			}

			if (!Part.Mesh && Part.ShapeBones.IsEmpty())
			{
				Problems.Add(FText::Format(LOCTEXT("NoShape", "{0}: 메시 없는 부품에 ShapeMeshParameter 가 있는데 ShapeBones 가 비었다 - 몸 전체를 따오지 않는다."),
					UEnum::GetDisplayValueAsText(Pair.Key)));
			}

			for (const UNiagaraSystem* Effect : {Part.FormationEffect.Get(), Part.IdleEffect.Get()})
			{
				if (Effect && !ExposesUserParameter(*Effect, Part.ShapeMeshParameter))
				{
					Problems.Add(FText::Format(LOCTEXT("NoShapeParam", "{0}: {1} 에 User 파라미터 {2} 가 없다 - 모양이 말없이 무시된다."),
						UEnum::GetDisplayValueAsText(Pair.Key), FText::FromString(Effect->GetName()),
						FText::FromName(Part.ShapeMeshParameter)));
				}
			}
		}
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBWeaponVisualConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
