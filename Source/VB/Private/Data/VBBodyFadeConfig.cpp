#include "Data/VBBodyFadeConfig.h"
#include "Materials/Material.h"
#include "Materials/MaterialInterface.h"
#include "Data/VBDataValidation.h"

#define LOCTEXT_NAMESPACE "VBBodyFade"
const FName UVBBodyFadeConfig::CoverageParameter(TEXT("BodyFadeCoverage"));

float UVBBodyFadeConfig::CoverageAt(float Progress) const
{
	if (!FMath::IsFinite(Progress) || !FMath::IsFinite(MinimumCoverage)
		|| !FMath::IsFinite(FadeInFraction) || FadeInFraction <= 0.0f || FadeInFraction >= 1.0f)
	{
		return 1.0f;
	}

	const float Phase = FMath::Clamp(Progress, 0.0f, 1.0f);
	const float Weight = Phase < FadeInFraction ? Phase / FadeInFraction : (1.0f - Phase) / (1.0f - FadeInFraction);
	return FMath::Lerp(1.0f, FMath::Clamp(MinimumCoverage, 0.0f, 1.0f), Weight);
}

UMaterialInterface* UVBBodyFadeConfig::FindVariant(const UMaterialInterface* Original) const
{
	if (!Original)
	{
		return nullptr;
	}

	const UMaterial* Master = Original->GetMaterial();
	for (const FVBBodyFadeMaterial& Entry : Materials)
	{
		if (Entry.Source == Master)
		{
			return Entry.Faded;
		}
	}

	return nullptr;
}

TArray<FText> UVBBodyFadeConfig::FindProblems() const
{
	TArray<FText> Problems;
	if (!FMath::IsFinite(MinimumCoverage) || MinimumCoverage < 0.0f || MinimumCoverage > 1.0f
		|| !FMath::IsFinite(FadeInFraction) || FadeInFraction <= 0.0f || FadeInFraction >= 1.0f)
	{
		Problems.Add(LOCTEXT("Envelope", "Body coverage must be in [0,1] and fade-in fraction strictly between 0 and 1."));
	}

	if (Materials.IsEmpty())
	{
		Problems.Add(LOCTEXT("Empty", "Body fade requires explicit material mappings."));
	}

	TSet<const UMaterial*> Seen;
	for (const FVBBodyFadeMaterial& Entry : Materials)
	{
		const UMaterial* Faded = Entry.Faded ? Entry.Faded->GetMaterial() : nullptr;
		const auto Report = [&Problems, &Entry](const FText& Reason)
		{
			Problems.Add(FText::Format(LOCTEXT("MappingProblem", "{0}: {1}"), FText::FromString(GetPathNameSafe(Entry.Faded)), Reason));
		};
		if (!Entry.Source || !Faded)
		{
			Report(LOCTEXT("MissingMapping", "Both source master and faded variant are required."));
			continue;
		}

		if (Entry.Source == Faded || Seen.Contains(Entry.Source))
		{
			Report(LOCTEXT("DuplicateMapping", "Each source master requires one distinct faded variant."));
		}

		Seen.Add(Entry.Source);
		if (Entry.Source->MaterialDomain != MD_Surface || Faded->MaterialDomain != MD_Surface)
		{
			Report(LOCTEXT("SurfaceMapping", "Source and faded variant must use the Surface domain."));
		}

		if (Entry.Faded->GetBlendMode() != BLEND_Masked)
		{
			Report(LOCTEXT("EffectiveBlend", "Variant must render as Masked; the raw BlendMode field alone is insufficient."));
		}

		if (!Faded->GetUsageByFlag(MATUSAGE_SkeletalMesh))
		{
			Report(LOCTEXT("SkeletalUsage", "Faded variant must support skeletal meshes."));
		}

		float Coverage = 0.0f;
		if (!Entry.Faded->GetScalarParameterValue(FHashedMaterialParameterInfo(CoverageParameter), Coverage))
		{
			Report(LOCTEXT("CoverageParameter", "Faded variant is missing BodyFadeCoverage."));
		}
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBBodyFadeConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif
#undef LOCTEXT_NAMESPACE
