// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Character/VBWeaponManifest.h"

#include "Engine/StaticMesh.h"
#include "Materials/MaterialInstanceDynamic.h"

namespace VBWeaponManifest
{
	namespace
	{
		const FName ProgressName(TEXT("ManifestProgress"));
		const FName RevealAxisName(TEXT("RevealAxis"));
		const FName RevealMinName(TEXT("RevealMin"));
		const FName RevealLengthName(TEXT("RevealLength"));
	}

	FRevealRange MeasureReveal(const UStaticMesh* Mesh, const FVector& LocalAxis)
	{
		FRevealRange Range;
		Range.Axis = LocalAxis.GetSafeNormal();
		if (Range.Axis.IsNearlyZero())
		{
			Range.Axis = FVector::UpVector;
		}

		const FBox Bounds = Mesh ? Mesh->GetBoundingBox() : FBox(FVector(-5), FVector(5));
		Range.BoundsCenter = Bounds.GetCenter();
		const float HalfLength = FVector::DotProduct(Bounds.GetExtent(), Range.Axis.GetAbs());
		Range.Min = FVector::DotProduct(Range.BoundsCenter, Range.Axis) - HalfLength;
		Range.Max = Range.Min + FMath::Max(2.0f * HalfLength, 1.0f);
		return Range;
	}

	FVector LongestAxis(const UStaticMesh* Mesh)
	{
		const FVector Extent = Mesh ? Mesh->GetBoundingBox().GetExtent() : FVector::ZeroVector;
		if (Extent.Y >= Extent.X && Extent.Y >= Extent.Z)
		{
			return FVector::RightVector;
		}

		return Extent.X >= Extent.Z ? FVector::ForwardVector : FVector::UpVector;
	}

	bool UsesManifest(const UMaterialInterface* Material)
	{
		float Unused = 0.0f;
		return Material && Material->GetScalarParameterValue(FHashedMaterialParameterInfo(ProgressName), Unused);
	}

	void ApplyReveal(UMaterialInstanceDynamic& Material, const FRevealRange& Range)
	{
		Material.SetVectorParameterValue(RevealAxisName, FLinearColor(Range.Axis));
		Material.SetScalarParameterValue(RevealMinName, Range.Min);
		Material.SetScalarParameterValue(RevealLengthName, Range.Max - Range.Min);
	}

	void SetProgress(UMaterialInstanceDynamic& Material, float Progress)
	{
		Material.SetScalarParameterValue(ProgressName, Progress);
	}
}
