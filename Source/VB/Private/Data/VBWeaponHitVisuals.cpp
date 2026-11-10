// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBWeaponHitVisuals.h"

#include "AbilitySystem/VBGameplayTags.h"
#include "NiagaraSystem.h"
#include "Types/VBNiagaraParameters.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBWeaponHitVisuals"

const FName UVBWeaponHitVisuals::RippleScaleParameter(TEXT("User.RippleScale"));
const FName UVBWeaponHitVisuals::RippleDurationParameter(TEXT("User.RippleDuration"));
const FName UVBWeaponHitVisuals::RippleColorParameter(TEXT("User.RippleColor"));
const FName UVBWeaponHitVisuals::CutLineWidthParameter(TEXT("User.CutLineWidth"));
const FName UVBWeaponHitVisuals::CutLineLengthParameter(TEXT("User.CutLineLength"));
const FName UVBWeaponHitVisuals::CutLineDurationParameter(TEXT("User.CutLineDuration"));
const FName UVBWeaponHitVisuals::CutLineColorParameter(TEXT("User.CutLineColor"));
const FName UVBWeaponHitVisuals::StrikeScaleParameter(TEXT("User.StrikeScale"));
const FName UVBWeaponHitVisuals::StrikeColorParameter(TEXT("User.StrikeColor"));

TArray<FText> UVBWeaponHitVisuals::FindProblems() const
{
	TArray<FText> Problems;
	if (!RippleSystem && !BloodSystem && !CutLineSystem && StrikeRows.IsEmpty())
	{
		Problems.Add(LOCTEXT("Empty", "이펙트가 하나도 없다 - 무기 정의에서 이 자산을 빼면 지금 모습으로 돌아간다."));
	}

	auto Require = [&Problems](const UNiagaraSystem* System, FName Parameter, bool bColor)
	{
		const bool bHas = bColor ? VBNiagaraParameters::HasUserColor(*System,
			Parameter) : VBNiagaraParameters::HasUserFloat(*System, Parameter);
		if (!bHas)
		{
			Problems.Add(FText::Format(LOCTEXT("MissingParameter", "{0} 에 {1} 가 없다 - 큐가 넣는 값이 말없이 무시된다."),
				FText::FromString(System->GetName()), FText::FromName(Parameter)));
		}
	};
	if (RippleSystem)
	{
		Require(RippleSystem, RippleScaleParameter, false);
		Require(RippleSystem, RippleDurationParameter, false);
		Require(RippleSystem, RippleColorParameter, true);
	}

	if (CutLineSystem)
	{
		Require(CutLineSystem, CutLineWidthParameter, false);
		Require(CutLineSystem, CutLineLengthParameter, false);
		Require(CutLineSystem, CutLineDurationParameter, false);
		Require(CutLineSystem, CutLineColorParameter, true);
	}

	const FGameplayTag ShapeRoot = VBGameplayTags::Melee_Shape;
	for (int32 Index = 0; Index < StrikeRows.Num(); ++Index)
	{
		const FVBStrikeVisualRow& Row = StrikeRows[Index];
		if (Row.Shapes.IsEmpty())
		{
			Problems.Add(FText::Format(LOCTEXT("StrikeNoShape", "부위 이펙트 {0}번 줄에 부위가 없다 - 어느 타격에도 나오지 않는다."), Index));
		}

		for (const FGameplayTag& Shape : Row.Shapes)
		{
			if (!Shape.MatchesTag(ShapeRoot) || Shape == ShapeRoot)
			{
				Problems.Add(FText::Format(LOCTEXT("StrikeBadShape", "부위 이펙트 {0}번 줄의 {1} 는 Melee.Shape 아래 부위가 아니다."),
					Index, FText::FromName(Shape.GetTagName())));
			}
		}

		if (Row.Impacts.Contains(EVBHitImpact::None))
		{
			Problems.Add(FText::Format(LOCTEXT("StrikeNoneImpact", "부위 이펙트 {0}번 줄의 등급 None 은 공격이 선언하지 않는다 - 그 칸은 맞지 않는다."), Index));
		}

		if (!Row.System)
		{
			Problems.Add(FText::Format(LOCTEXT("StrikeNoSystem", "부위 이펙트 {0}번 줄에 이펙트가 없다."), Index));
			continue;
		}

		Require(Row.System, StrikeScaleParameter, false);
		Require(Row.System, StrikeColorParameter, true);
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBWeaponHitVisuals::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
