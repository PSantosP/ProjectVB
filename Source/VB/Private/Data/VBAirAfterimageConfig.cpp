// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBAirAfterimageConfig.h"
#include "Data/VBBodyFadeConfig.h"

#include "Data/VBCombatFeedbackProfile.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAirAfterimageConfig"

const FName UVBAirAfterimageConfig::OpacityParameter(TEXT("GhostOpacity"));
const FName UVBAirAfterimageConfig::TintParameter(TEXT("GhostTint"));
const FName UVBAirAfterimageConfig::FresnelParameter(TEXT("GhostFresnelStrength"));

float UVBAirAfterimageConfig::OpacityAt(float Age) const
{
	if (!FMath::IsFinite(Age) || !FMath::IsFinite(GhostLifetime) || GhostLifetime <= 0.0f
		|| !FMath::IsFinite(StartOpacity) || Age < 0.0f || Age >= GhostLifetime)
	{
		return 0.0f;
	}

	const float Alpha = Age / GhostLifetime;
	const FRichCurve* Curve = OpacityCurve.GetRichCurveConst();
	const float Scale = Curve && Curve->GetNumKeys() > 0 ? Curve->Eval(Alpha) : 1.0f - Alpha;
	return FMath::IsFinite(Scale) ? FMath::Clamp(StartOpacity * Scale, 0.0f, 1.0f) : 0.0f;
}

TArray<FText> UVBAirAfterimageConfig::FindProblems() const
{
	TArray<FText> Problems;
	if (BodyFade)
	{
		Problems.Append(BodyFade->FindProblems());
	}

	if (MaxGhosts < 1 || MaxGhosts > 8 || MaxPartsPerGhost < 1 || MaxPartsPerGhost > 16
		|| static_cast<int64>(MaxGhosts) * MaxPartsPerGhost > MaxSnapshotMeshes)
	{
		Problems.Add(LOCTEXT("PoolBudget", "Ghost groups and parts must fit the 32-mesh budget (1-8 groups, 1-16 parts)."));
	}

	if (!FMath::IsFinite(GhostLifetime) || GhostLifetime <= 0.0f)
	{
		Problems.Add(LOCTEXT("Lifetime", "Ghost lifetime must be finite and positive."));
	}

	if (!FMath::IsFinite(SampleInterval) || SampleInterval <= 0.0f
		|| !FMath::IsFinite(MinSampleDistance) || MinSampleDistance <= 0.0f)
	{
		Problems.Add(LOCTEXT("Sampling", "Sample interval and minimum distance must be finite and positive."));
	}

	if (!FMath::IsFinite(StartOpacity) || StartOpacity < 0.0f || StartOpacity > 1.0f
		|| !FMath::IsFinite(FresnelStrength) || FresnelStrength < 0.0f
		|| !FMath::IsFinite(Tint.R) || !FMath::IsFinite(Tint.G) || !FMath::IsFinite(Tint.B) || !FMath::IsFinite(Tint.A))
	{
		Problems.Add(LOCTEXT("Appearance", "Opacity must be in [0,1]; tint and nonnegative Fresnel strength must be finite."));
	}

	const FRichCurve* Curve = OpacityCurve.GetRichCurveConst();
	if (Curve && Curve->GetNumKeys() > 0)
	{
		float Min = 0.0f, Max = 0.0f;
		Curve->GetValueRange(Min, Max);
		if (!FMath::IsFinite(Min) || !FMath::IsFinite(Max) || Min < 0.0f || Max > 1.0f
			|| !FMath::IsNearlyEqual(Curve->Eval(0.0f), 1.0f) || !FMath::IsNearlyZero(Curve->Eval(1.0f)))
		{
			Problems.Add(LOCTEXT("OpacityCurve", "Opacity curve must stay in [0,1], start at 1 and finish at 0."));
		}
	}

	if (GhostMaterial)
	{
		const UMaterial* Base = GhostMaterial->GetMaterial();
		if (!Base || Base->MaterialDomain != MD_Surface || GhostMaterial->GetBlendMode() != BLEND_Translucent
			|| !Base->GetUsageByFlag(MATUSAGE_SkeletalMesh))
		{
			Problems.Add(LOCTEXT("MaterialUsage", "Ghost material must be a translucent surface with skeletal-mesh usage enabled."));
		}

		float Scalar = 0.0f;
		FLinearColor Color;
		if (!GhostMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(OpacityParameter), Scalar))
		{
			Problems.Add(LOCTEXT("OpacityParameter", "Ghost material is missing GhostOpacity."));
		}

		if (!GhostMaterial->GetVectorParameterValue(FHashedMaterialParameterInfo(TintParameter), Color))
		{
			Problems.Add(LOCTEXT("TintParameter", "Ghost material is missing GhostTint."));
		}

		if (!GhostMaterial->GetScalarParameterValue(FHashedMaterialParameterInfo(FresnelParameter), Scalar))
		{
			Problems.Add(LOCTEXT("FresnelParameter", "Ghost material is missing GhostFresnelStrength."));
		}
	}

	if (WhooshProfile && (!WhooshProfile->bScreenImpact || !WhooshProfile->WorldEffects.IsEmpty()
		|| !WhooshProfile->SlowMotionSteps.IsEmpty()))
	{
		Problems.Add(LOCTEXT("ScreenOnly", "Chase feedback must enable screen impact and contain no world effects or slow-motion steps."));
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBAirAfterimageConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
