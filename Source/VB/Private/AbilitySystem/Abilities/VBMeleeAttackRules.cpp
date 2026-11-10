// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AbilitySystem/Abilities/VBMeleeAttackRules.h"

#include "Animation/AnimMontage.h" // ExtractRootMotionFromTrackRange
#include "Animation/VBMotionWarpingNames.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Character/VBTargetable.h"
#include "Data/VBAttackConfig.h"
#include "RootMotionModifier.h" // URootMotionModifier_Warp::WarpTargetName

float VBMeleeAttackRules::ResolveApproachOffset(const FVBApproachOffsetInputs& In)
{
	// 기본 멈춤과 사거리는 같은 단위(둘 다 캐릭터 배율이 곱해졌다).
	float ApproachOffset = FMath::Clamp(In.BaseOffset, 0.0f, FMath::Max(0.0f, In.ScaledRange));
	// 먼 시작(갭 클로저): 적이 사거리 밖에서 시작한 스윙은 더 가까이 붙는다 - 붙은 것이 눈에 보이게. FarStartApproachOffset=0 이면 없음.
	if (In.FarStartApproachOffset > 0.0f && In.DistToTarget2D > In.ScaledRange)
	{
		ApproachOffset = FMath::Clamp(In.FarStartApproachOffset, 0.0f, ApproachOffset);
	}

	// 상한: 오프셋이 지금 거리를 넘으면 워프 타겟이 플레이어 뒤에 생겨 SkewWarp 가 뒤로 돌아선다.
	// MinWarpStandoff 만큼 빼 타겟이 늘 적 쪽에 있게 한다.
	ApproachOffset = FMath::Min(ApproachOffset, FMath::Max(0.0f, In.DistToTarget2D - In.MinWarpStandoff));

	// 하한: 두 캡슐 반경 합보다 가까운 목표는 CMC 가 매 프레임 밀어내 떨린다(카메라 흔들림으로 보인다).
	// 이미 겹쳤으면 지금 거리를 그대로 써 전진량 0 으로 둔다(하한을 그대로 걸면 타겟이 뒤로 간다).
	// 하한이 마지막이라 사거리 상한보다 앞선다.
	if (In.MinSeparation > 0.0f)
	{
		ApproachOffset = FMath::Max(ApproachOffset, FMath::Min(In.MinSeparation, In.DistToTarget2D));
	}

	return ApproachOffset;
}

float VBMeleeAttackRules::MeasureMontageLungeCm(const UAnimMontage* Montage, float MeshScale)
{
	if (!Montage)
	{
		return 0.0f;
	}
	// 재는 구간 = 처음부터 접근 워프 창이 닫힐 때까지. 창 뒤 루트모션은 워프와 무관하게 돌므로 세지 않는다
	// (세면 시작 거리가 멀어져 워프가 순간이동처럼 보인다). 창이 없으면 클립 끝까지, 루트모션이 꺼져 있으면 0.
	float EndTime = Montage->GetPlayLength();
	float WindowEnd = 0.0f;
	if (FindApproachWarpModifier(Montage, &WindowEnd))
	{
		EndTime = FMath::Min(EndTime, WindowEnd);
	}

	FAnimExtractContext Context(0.0, /*bExtractRootMotion*/ true);
	const FTransform RootMotion = Montage->ExtractRootMotionFromTrackRange(0.0f, EndTime, Context);
	return static_cast<float>(RootMotion.GetTranslation().Size2D()) * MeshScale;
}

const URootMotionModifier_Warp* VBMeleeAttackRules::FindApproachWarpModifier(const UAnimMontage* Montage, float* OutWindowEndTime)
{
	if (!Montage)
	{
		return nullptr;
	}

	for (const FAnimNotifyEvent& Event : Montage->Notifies)
	{
		const UAnimNotifyState_MotionWarping* WarpState = Cast<UAnimNotifyState_MotionWarping>(Event.NotifyStateClass);
		const URootMotionModifier_Warp* Modifier = WarpState ? Cast<URootMotionModifier_Warp>(WarpState->RootMotionModifier) : nullptr;
		if (Modifier && Modifier->WarpTargetName == VBMotionWarpingNames::AttackTarget)
		{
			if (OutWindowEndTime)
			{
				*OutWindowEndTime = Event.GetTriggerTime() + Event.GetDuration();
			}

			return Modifier;
		}
	}

	return nullptr;
}

FName VBMeleeAttackRules::ComboSectionName(const UVBAttackConfig* Config, int32 ComboSectionIndex)
{
	// 설정의 섹션 이름 배열 우선(무기별 섹션명), 미설정/범위 밖이면 레거시 "Swing%d"(회귀 안전).
	if (Config && Config->ComboSectionNames.IsValidIndex(ComboSectionIndex))
	{
		return Config->ComboSectionNames[ComboSectionIndex];
	}

	return FName(*FString::Printf(TEXT("Swing%d"), ComboSectionIndex + 1));
}

bool VBMeleeAttackRules::HasComboSection(const UAnimMontage* Montage, const UVBAttackConfig* Config, int32 ComboSectionIndex)
{
	return Montage && Montage->IsValidSectionName(ComboSectionName(Config, ComboSectionIndex));
}

FVector VBMeleeAttackRules::ResolveApproachSyncPoint(const FVector& OwnerLocation, const FVector& OwnerForward,
	const FVector& TargetLocation,
	const FVector& Goal, float MinLead, bool& bOutHold)
{
	FVector ToTarget = (TargetLocation - OwnerLocation).GetSafeNormal2D();
	if (ToTarget.IsNearlyZero())
	{
		ToTarget = OwnerForward.GetSafeNormal2D(); // 대상과 겹쳤다 - 볼 방향이 없으니 지금 정면을 지킨다
	}
	// 앞섬은 대상 방향 성분만 센다. 옆으로 비킨 고정 목표(적이 정한 한 점)는 앞서 있는 한 그대로 본다 - 그 점을 보는 것이 고정 목표의 뜻이다.
	const double Lead = FVector::DotProduct(Goal - OwnerLocation, ToTarget);
	bOutHold = Lead < MinLead;
	return bOutHold ? OwnerLocation + ToTarget * MinLead : Goal;
}

void VBMeleeAttackRules::ResolveApproachGoal(bool bFollows, const FVector& OwnerLocation, const FVector& LiveTargetLocation,
	float StopDistance,
	const FVector& FixedGoal, const FVector& FixedAim, FVector& OutGoal, FVector& OutAim)
{
	if (bFollows)
	{
		OutGoal = LiveTargetLocation + (OwnerLocation - LiveTargetLocation).GetSafeNormal2D() * StopDistance;
		OutAim = LiveTargetLocation;
		return;
	}
	// 제자리 클립(전진 0)은 고정 목표가 곧 공격자 자리라 첫 프레임부터 붙잡힌다. 지금 대상을 겨냥하면 스윙 내내 플레이어를 따라 돈다.
	OutGoal = FixedGoal;
	OutAim = FixedAim;
}

bool VBMeleeAttackRules::CanKeepAttackWarp(const AActor* SwingTarget)
{
	if (!IsValid(SwingTarget))
	{
		return false;
	}

	const IVBTargetable* Targetable = Cast<const IVBTargetable>(SwingTarget);
	return !Targetable || Targetable->IsTargetable();
}

EVBAttackSide VBMeleeAttackRules::ResolveTargetSide(float TargetLocalY)
{
	return TargetLocalY >= 0.0f ? EVBAttackSide::Right : EVBAttackSide::Left;
}

TArray<bool> VBMeleeAttackRules::FilterVariantsBySide(TConstArrayView<EVBAttackSide> Sides, EVBAttackSide TargetSide, bool& bOutNoneOnSide)
{
	bOutNoneOnSide = false;
	TArray<bool> Allowed;
	Allowed.Init(true, Sides.Num());
	if (!Sides.ContainsByPredicate([](EVBAttackSide Side) { return Side != EVBAttackSide::Any; }))
	{
		return Allowed;
	}

	int32 AllowedCount = 0;
	for (int32 Index = 0; Index < Sides.Num(); ++Index)
	{
		Allowed[Index] = Sides[Index] == EVBAttackSide::Any || Sides[Index] == TargetSide;
		AllowedCount += Allowed[Index] ? 1 : 0;
	}

	if (AllowedCount == 0)
	{
		bOutNoneOnSide = true;
		Allowed.Init(true, Sides.Num());
	}

	return Allowed;
}

EVBPlannedVariant VBMeleeAttackRules::JudgePlannedVariant(const FVBEnemyVariantFit& Fit, int32 Planned)
{
	if (!Fit.Allowed.IsValidIndex(Planned) || !Fit.Allowed[Planned])
	{
		return EVBPlannedVariant::Unusable;
	}

	return Fit.InBand(Planned) ? EVBPlannedVariant::InBand : EVBPlannedVariant::OutOfBand;
}

int32 VBMeleeAttackRules::PickBestEnemyVariant(const FVBEnemyVariantFit& Fit, bool& bOutInBand)
{
	int32 Best = INDEX_NONE;
	for (int32 Index = 0; Index < Fit.Allowed.Num(); ++Index)
	{
		if (Fit.Allowed[Index] && Fit.InBand(Index) && (Best == INDEX_NONE || Fit.Lunges[Index] > Fit.Lunges[Best]))
		{
			Best = Index;
		}
	}

	bOutInBand = Best != INDEX_NONE;
	if (!bOutInBand)
	{
		for (int32 Index = 0; Index < Fit.Allowed.Num(); ++Index)
		{
			if (Fit.Allowed[Index] && (Best == INDEX_NONE || Fit.Distortion(Index) < Fit.Distortion(Best)))
			{
				Best = Index;
			}
		}
	}

	return Best;
}
