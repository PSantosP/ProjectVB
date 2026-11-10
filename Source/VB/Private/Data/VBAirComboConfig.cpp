// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBAirComboConfig.h"

#include "Animation/AnimMontage.h"
#include "Curves/CurveFloat.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBAirAfterimageConfig.h"

#if WITH_EDITOR
#include "Data/VBDataValidation.h"
#endif

#define LOCTEXT_NAMESPACE "VBAirComboConfig"

bool UVBAirComboConfig::IsSlamMotionValid() const
{
	return FMath::IsFinite(SlamFallSpeed) && SlamFallSpeed > 0.0f && FMath::IsFinite(SlamMaxSeconds) && SlamMaxSeconds > 0.0f;
}

bool UVBAirComboConfig::HasReachedChaseTarget(const FVector& Location, const FVector& TargetLocation) const
{
	return FMath::IsFinite(ChaseArrivalTolerance) && ChaseArrivalTolerance > 0.0f
		&& !Location.ContainsNaN() && !TargetLocation.ContainsNaN()
		&& FVector::DistSquared(Location, TargetLocation) <= FMath::Square(ChaseArrivalTolerance);
}

bool UVBAirComboConfig::HasFiniteSlamRecovery() const
{
	const UAnimMontage* Montage = StringAttack ? StringAttack->AttackMontage.Get() : nullptr;
	const int32 EndIndex = Montage ? Montage->GetSectionIndex(SlamEndSection) : INDEX_NONE;
	return HasSlam() && EndIndex != INDEX_NONE && Montage->bEnableAutoBlendOut
		&& Montage->CompositeSections[EndIndex].NextSectionName.IsNone();
}

TArray<FText> UVBAirComboConfig::FindProblems() const
{
	TArray<FText> Problems;
	if (!StringCameraFraming.IsValid())
	{
		Problems.Add(LOCTEXT("InvalidCameraFraming", "공중 카메라 구도의 각도와 추적 속도를 확인해야 한다."));
	}

	if (bSlamOnFinalHit && !SlamImpactCue.IsValid())
	{
		Problems.Add(LOCTEXT("MissingStringFinish", "마지막 타격 내려찍기에는 바닥 충격 큐가 필요하다."));
	}

	if ((HasSlam() || bSlamOnFinalHit) && (!FMath::IsFinite(SlamMaxSeconds) || SlamMaxSeconds <= 0.0f))
	{
		Problems.Add(LOCTEXT("InvalidSlamTimeout", "내려찍기의 착지 제한 시간은 유한한 양수여야 한다."));
	}

	if (Staging)
	{
		Problems.Append(Staging->FindProblems());
	}

	if (!ChaseTakeoffMontage || !ChaseMontage)
	{
		Problems.Add(LOCTEXT("NoChaseMontage", "이륙 · 쫓기 몽타주가 비었다 - 쫓는 동안 몸이 서 있는 자세로 날아간다."));
	}

	if (!StringAttack || !StringAttack->AttackMontage)
	{
		Problems.Add(LOCTEXT("NoString", "공중 연타 잎(StringAttack)이나 그 몽타주가 비었다 - 도착해도 칠 것이 없다."));
	}

	float FollowMin = 0.0f, FollowMax = 0.0f;
	if (StringFollowTimeMapping)
	{
		StringFollowTimeMapping->FloatCurve.GetValueRange(FollowMin, FollowMax);
	}

	if (!StringFollowTimeMapping || !FMath::IsNearlyEqual(FollowMin, 1.0f) || !FMath::IsNearlyEqual(FollowMax, 1.0f))
	{
		Problems.Add(LOCTEXT("NoStringFollow", "연타 추적의 시간 곡선은 값이 항상 1이어야 한다."));
	}

	if (StringAttack && !StringAttack->MontageVariants.IsEmpty())
	{
		// 내려찍기 섹션, 연타 섹션이 한 몽타주에 있어야 하고, 서버와 클라가 같은 몽타주를 골라야 한다(변형은 기계마다 따로 고른다).
		Problems.Add(LOCTEXT("StringVariants", "공중 연타 잎에 몽타주 변형이 있다 - 공중 연타는 AttackMontage 하나만 쓴다."));
	}

	if (StringAttack && StringAttack->ComboSectionNames.Num() != StringAttack->MaxComboCount)
	{
		Problems.Add(LOCTEXT("StringSections", "공중 연타 잎의 섹션 이름 수가 MaxComboCount 와 다르다."));
	}

	if (StringAttack && StringAttack->AttackMontage && HasSlam())
	{
		// Montage_JumpToSection 은 없는 섹션에도 조용히 실패한다 - 저장 때 잡는다.
		const UAnimMontage* Montage = StringAttack->AttackMontage;
		for (const FName Section : {SlamStartSection, SlamLoopSection, SlamEndSection})
		{
			if (Montage->GetSectionIndex(Section) == INDEX_NONE)
			{
				Problems.Add(FText::Format(LOCTEXT("SlamSection", "내려찍기 섹션 '{0}' 가 공중 연타 몽타주에 없다."), FText::FromName(Section)));
			}
		}

		if (Montage->GetSectionIndex(SlamEndSection) != INDEX_NONE && !HasFiniteSlamRecovery())
		{
			Problems.Add(LOCTEXT("InfiniteSlamRecovery", "착지 섹션의 다음 링크를 비우고 몽타주의 자동 블렌드 아웃을 켜야 한다."));
		}
		// 연타 마지막 섹션이 내려찍기로 이어지면 누르지 않아도 내려찍는다.
		const int32 LastIndex = StringAttack->ComboSectionNames.IsEmpty() ? INDEX_NONE
			: Montage->GetSectionIndex(StringAttack->ComboSectionNames.Last());
		if (LastIndex != INDEX_NONE && Montage->CompositeSections[LastIndex].NextSectionName == SlamStartSection)
		{
			Problems.Add(LOCTEXT("SlamAutoLink", "연타 마지막 섹션이 내려찍기 시작으로 이어져 있다 - 다음 섹션 링크를 끊는다."));
		}
	}

	if ((HasSlam() || bSlamOnFinalHit) && (!FMath::IsFinite(SlamFallSpeed) || SlamFallSpeed <= 0.0f))
	{
		Problems.Add(LOCTEXT("InvalidSlamFallSpeed", "내려찍기 낙하 속도는 유한한 양수여야 한다."));
	}
	// 시간은 공격자가 쫓는 동안 떠 있는 시간보다 짧아야 한다 - 도착 전에 떨어지면 연타가 허공을 친다.
	if (ChaseSeconds >= AttackerMaxAirSeconds)
	{
		Problems.Add(LOCTEXT("ChaseTooLong", "ChaseSeconds 가 AttackerMaxAirSeconds 이상이다 - 도착하기 전에 떨어진다."));
	}

	if (!FMath::IsFinite(ChaseSeconds) || ChaseSeconds <= 0.0f
		|| !FMath::IsFinite(ChaseArrivalTolerance) || ChaseArrivalTolerance <= 0.0f)
	{
		Problems.Add(LOCTEXT("InvalidChaseArrival", "추격 시간과 도착 허용 오차는 유한한 양수여야 한다."));
	}

	if (ChaseStopDistance >= MaxChaseDistance)
	{
		Problems.Add(LOCTEXT("StopBeyondReach", "ChaseStopDistance 가 MaxChaseDistance 이상이다 - 쫓을 거리가 없다."));
	}

	return Problems;
}

#if WITH_EDITOR
EDataValidationResult UVBAirComboConfig::IsDataValid(FDataValidationContext& Context) const
{
	return VBDataValidation::AddProblems(Super::IsDataValid(Context), FindProblems(), Context);
}
#endif

#undef LOCTEXT_NAMESPACE
