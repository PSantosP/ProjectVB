// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBInteractableConfig.h"

#include "Animation/AnimMontage.h"
#include "Animation/AnimSequence.h"
#include "Interaction/VBSetupValidation.h"

FString UVBInteractableConfig::ValidatePassage() const
{
	if (!bBlocksPassage)
	{
		return FString();
	}

	const FVector Normal = SidePlaneNormal.GetSafeNormal2D();
	if (!bUseSidePlane || Normal.IsNearlyZero())
	{
		return TEXT("길은 앞뒤 판정면이 있어야 한다(bUseSidePlane, SidePlaneNormal)");
	}

	struct FSide { const FVBInteractionMotion& Motion; const TCHAR* Name; bool bFront; };
	const FSide Sides[] = {
		{ActivateMotion, TEXT("ActivateMotion"), true}, {DeactivateMotion, TEXT("DeactivateMotion"), true},
		{ActivateMotionBack, TEXT("ActivateMotionBack"), false}, {DeactivateMotionBack, TEXT("DeactivateMotionBack"), false}};
	for (const FSide& Side : Sides)
	{
		if (!Side.Motion.CharacterMontage || !Side.Motion.PropAnimation)
		{
			return FString::Printf(TEXT("길은 양쪽 동작이 다 있어야 한다 - %s 의 몽타주나 소품 애니가 비었다"), Side.Name);
		}
		// SelectMotion 과 같은 공간(소품 루트 기준)에서 잰다. 설 자리가 반대편이면 닫힌 문을 지나 정렬하려다 막힌다.
		const bool bStandsFront = FVector::DotProduct(Side.Motion.StandTransform.GetLocation() - SidePlaneOrigin, Normal) >= 0.0f;
		if (!Side.Motion.bStandFromApproach && bStandsFront != Side.bFront)
		{
			return FString::Printf(TEXT("%s 의 설 자리가 판정면의 반대편에 있다"), Side.Name);
		}
	}

	return FString();
}

#if WITH_EDITOR
EDataValidationResult UVBInteractableConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBReportSetupProblem(Context, *this, ValidatePassage(), Super::IsDataValid(Context));
}
#endif

float FVBEnvironmentHandContact::GetWeight(float MontageTime) const
{
	if (PropBone.IsNone() || !FMath::IsFinite(MontageTime) || !FMath::IsFinite(StartTime)
		|| !FMath::IsFinite(EndTime) || !FMath::IsFinite(BlendTime)
		|| EndTime <= StartTime || MontageTime <= StartTime || MontageTime >= EndTime)
	{
		return 0.0f;
	}

	const float Ramp = FMath::Min(FMath::Max(BlendTime, 0.0f), (EndTime - StartTime) * 0.5f);
	if (Ramp <= KINDA_SMALL_NUMBER)
	{
		return 1.0f;
	}

	const float Weight = FMath::Clamp(FMath::Min(MontageTime - StartTime, EndTime - MontageTime) / Ramp, 0.0f, 1.0f);
	return Weight * Weight * (3.0f - 2.0f * Weight);
}

const FVBInteractionMotion* UVBInteractableConfig::GetMotionByIndex(uint8 Index) const
{
	switch (Index)
	{
	case 0: return &ActivateMotion;
	case 1: return &DeactivateMotion;
	case 2: return &ActivateMotionBack;
	case 3: return &DeactivateMotionBack;
	case 4: return &KickOpenMotion;
	default: return nullptr;
	}
}

uint8 UVBInteractableConfig::GetMotionIndex(const FVBInteractionMotion& Motion) const
{
	for (uint8 Index = 0; Index < 5; ++Index)
	{
		if (GetMotionByIndex(Index) == &Motion)
		{
			return Index;
		}
	}

	return MAX_uint8;
}

float UVBInteractableConfig::ResolveFastRate(bool bTargetWasActivated) const
{
	const float Rate = bTargetWasActivated ? FastCloseRate : FastOpenRate;
	return FMath::IsFinite(Rate) ? FMath::Clamp(Rate, 1.0f, 3.0f) : 1.0f;
}

const FVBInteractionMotion* UVBInteractableConfig::SelectMotion(bool bActivating, const FTransform& PropTransform,
	const FVector& InteractorLocation, bool bSprinting,
	const FVector& Velocity) const
{
	const FVBInteractionMotion& Front = bActivating ? ActivateMotion : DeactivateMotion;
	const FVBInteractionMotion& Back = bActivating ? ActivateMotionBack : DeactivateMotionBack;

	const bool bHasFront = Front.HasCharacterMotion();
	const bool bHasBack = Back.HasCharacterMotion();
	const bool bHasPlane = bUseSidePlane && !SidePlaneNormal.GetSafeNormal2D().IsNearlyZero();
	const FVector FrontStand = PropTransform.TransformPosition(Front.StandTransform.GetLocation());
	const FVector BackStand = PropTransform.TransformPosition(Back.StandTransform.GetLocation());
	const bool bFront = bHasPlane
		? FVector::DotProduct(PropTransform.InverseTransformPosition(InteractorLocation) - SidePlaneOrigin,
			SidePlaneNormal.GetSafeNormal2D()) >= 0.0f
		: FVector::DistSquared2D(InteractorLocation, FrontStand) <= FVector::DistSquared2D(InteractorLocation, BackStand);

	// 서버 속도를 접근 방향에 투영해 KickMinSpeed 와 비교한다. 0 보다 크면 질주 키만 누른 정지/후진은 손 동작이고,
	//   0 이면(문 데이터가 고른다) 서서도 차되 뒤로 물러나는 중(투영이 음수)은 여전히 손 동작이다.
	const FVector KickForward = (KickOpenMotion.StandTransform * PropTransform).GetUnitAxis(EAxis::X).GetSafeNormal2D();
	if (bHasPlane && bActivating && bSprinting && bFront == bKickFromFront
		&& KickOpenMotion.HasCharacterMotion() && KickOpenMotion.PropAnimation
		&& FVector::DotProduct(Velocity, KickForward) >= FMath::Max(KickMinSpeed, 0.0f) - KINDA_SMALL_NUMBER)
	{
		return &KickOpenMotion;
	}

	if (bHasPlane)
	{
		return bFront ? (bHasFront ? &Front : nullptr) : (bHasBack ? &Back : nullptr);
	}

	if (!bHasFront && !bHasBack)
	{
		return nullptr;
	}

	if (bHasFront != bHasBack)
	{
		return bHasFront ? &Front : &Back;
	}

	return bFront ? &Front : &Back;
}
