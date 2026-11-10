// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBEnemyConfig.h"
#include "Animation/AnimMontage.h" // IsDataValid - 사망 몽타주 길이

// 접근 시간 식의 홈. 에디터 전용이 아니다 - 단위 테스트(비에디터 빌드 포함)도 이 함수를 부른다.
float UVBEnemyConfig::GetNormalApproachSeconds() const
{
	const float ApproachDistance = SightRadius - AttackCommitDistance;
	// 마지막 구간은 걷는다. 뛰기에서 걸음으로 떨어지는 지점이 RunExitDistance 이므로 그것이 걷는 구간이다.
	const float WalkLeg = FMath::Clamp(RunExitDistance, 0.0f, FMath::Max(0.0f, ApproachDistance));
	return (ApproachDistance - WalkLeg) / FMath::Max(1.0f, RunSpeed) + WalkLeg / FMath::Max(1.0f, WalkSpeed);
}

UAnimMontage* UVBEnemyConfig::PickDeathMontage(bool bWasDowned) const
{
	if (bWasDowned)
	{
		return DownedDeathMontage;
	}

	return DeathMontages.IsEmpty() ? nullptr : DeathMontages[FMath::RandRange(0, DeathMontages.Num() - 1)].Get();
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"   // FDataValidationContext::AddError
#include "Data/VBEnemyAnimSet.h"   // 걸음 속도를 대조할 상대 - AnimSet->Locomotion
#include "Animation/BlendSpace.h"  // GetBlendParameter / GetBlendSamples (UE 5.8 엔진 헤더에서 확인)
#include "Data/VBDeathDissolveConfig.h" // IsDataValid - 디졸브 길이가 정리 지연 안에 드는지
#include "Data/VBDeathDissolveSettings.h" // ResolveConfig - 비어 있으면 프로젝트 기본값
#include "Narrative/Story/VBRuleTypes.h"  // OnKilledEffects 칸 검사
#define LOCTEXT_NAMESPACE "VBEnemyConfig"

EDataValidationResult UVBEnemyConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);
	ValidateAttackRole(Context, Result);
	ValidateGaitSpeeds(Context, Result);
	ValidateLocomotionClips(Context, Result);
	ValidateApproachBudget(Context, Result);
	ValidateDeathTiming(Context, Result);

	// 처치 효과의 빈 칸, 엉뚱한 구조체는 실행 때 조용히 건너뛰어 드롭이 안 나오는 것으로만 보인다 - 저작 때 잡는다.
	if (!VBRules::ValidateStructArray(OnKilledEffects, FVBEffect::StaticStruct(),
		LOCTEXT("VBEnemy_OnKilledEffectsWhere", "OnKilledEffects"), Context))
	{
		Result = EDataValidationResult::Invalid;
	}

	return Result;
}

void UVBEnemyConfig::ValidateAttackRole(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	// 태그가 비면 그 적은 조용히 공격 후보에서 빠진다. 런타임 ValidateAttackAbilityContract 는 부여 결과를 보는 별개 검사다.
	if (!AttackAbilityInputTag.IsValid())
	{
		Context.AddError(LOCTEXT("VBEnemy_NoAttackInputTag",
			"AttackAbilityInputTag 가 비어 있습니다. 이 자산을 쓰는 적은 공격 토큰 후보가 되지 않아 "
			"영원히 공격하지 않습니다(역할이 Surrounder 로 고정됩니다)."));
		Result = EDataValidationResult::Invalid;
	}

	// 슈퍼아머 반격은 태그와 임계가 짝이다. 한쪽만 있으면 반격이 영영 안 나오고 로그도 없다.
	if (ArmoredCounterAttackInputTag.IsValid() != (ArmoredCounterAfterHits > 0))
	{
		Context.AddError(LOCTEXT("VBEnemy_ArmoredCounterHalf",
			"슈퍼아머 반격은 ArmoredCounterAttackInputTag 와 ArmoredCounterAfterHits(>0) 가 함께 있어야 합니다. "
			"한쪽만 저작되면 반격이 영영 나오지 않습니다."));
		Result = EDataValidationResult::Invalid;
	}

	// 클립 묶음이 비면 참조 포즈로 선다. 모든 적이 ABP_Enemy_Base 를 쓰므로 에러다.
	if (!AnimSet)
	{
		Context.AddError(LOCTEXT("VBEnemy_NoAnimSet",
			"AnimSet 이 비어 있습니다. 모든 적이 ABP_Enemy_Base 를 쓰므로 이 적은 참조 포즈로 서게 됩니다."));
		Result = EDataValidationResult::Invalid;
	}

	// Release > Commit 이어야 역할이 재계산 주기마다 왕복하지 않는다.
	if (AttackReleaseDistance <= AttackCommitDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_BadHysteresis",
			"AttackReleaseDistance({0}) 는 AttackCommitDistance({1}) 보다 커야 합니다. "
			"같거나 작으면 플레이어가 경계선에서 스트레이프할 때 역할이 매 재계산 주기마다 뒤집힙니다."),
			FText::AsNumber(AttackReleaseDistance), FText::AsNumber(AttackCommitDistance)));
		Result = EDataValidationResult::Invalid;
	}

	// 대기 거리가 공격 유지 거리 안쪽이면 대기자와 공격자를 거리로 구분할 수 없다.
	// 필요조건만 본다 - 엔진 실효 도착 반경(BT, 캡슐 값)은 이 자산이 볼 수 없다.
	const float WaitingDistance = AttackCommitDistance + WaitingStandoff;
	if (WaitingDistance <= AttackReleaseDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_WaitingInsideAttackRange",
			"대기 거리(AttackCommitDistance {0} + WaitingStandoff {1} = {2}) 가 "
			"AttackReleaseDistance({3}) 보다 커야 합니다. "
			"작으면 대기하는 적이 공격 사거리 안에 서서 공격자와 구분되지 않고, "
			"역할이 매 재계산 주기마다 뒤집힙니다. "
			"(엔진의 실효 도착 반경 때문에 실제 대기 거리는 이보다 더 안쪽입니다 - 여유를 충분히 두십시오.)"),
			FText::AsNumber(AttackCommitDistance), FText::AsNumber(WaitingStandoff),
			FText::AsNumber(WaitingDistance), FText::AsNumber(AttackReleaseDistance)));
		Result = EDataValidationResult::Invalid;
	}
}

void UVBEnemyConfig::ValidateGaitSpeeds(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	if (WalkSpeed >= RunSpeed)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_GaitOrder",
			"WalkSpeed({0}) 는 RunSpeed({1}) 보다 작아야 합니다. 같거나 크면 둘러쌀 때와 쫓아올 때의 "
			"걸음이 구분되지 않습니다."),
			FText::AsNumber(WalkSpeed), FText::AsNumber(RunSpeed)));
		Result = EDataValidationResult::Invalid;
	}

	// 진입 > 이탈 이어야 경계에서 걸음이 떨리지 않는다.
	if (RunExitDistance >= RunEnterDistance)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_GaitHysteresis",
			"RunExitDistance({0}) 는 RunEnterDistance({1}) 보다 작아야 합니다. 같거나 크면 경계에서 "
			"걸음이 매 주기 뒤집혀 다리가 떨립니다."),
			FText::AsNumber(RunExitDistance), FText::AsNumber(RunEnterDistance)));
		Result = EDataValidationResult::Invalid;
	}
}

void UVBEnemyConfig::ValidateLocomotionClips(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	// 속도가 맞아야 하는 상대는 이 적의 클립이라 블렌드스페이스 축 최대와 샘플 좌표를 실제로 읽는다.
	if (AnimSet && AnimSet->Locomotion)
	{
		const UBlendSpace* Locomotion = AnimSet->Locomotion;

		// 축 1(Y)이 속도다(UVBEnemyAnimSet 계약). 1D 블렌드스페이스면 미사용 축 0..100 이라 아래 최대값 검사에 걸린다.
		const FBlendParameter& SpeedAxis = Locomotion->GetBlendParameter(1);

		if (RunSpeed > SpeedAxis.Max)
		{
			Context.AddError(FText::Format(LOCTEXT("VBEnemy_RunSpeedOverAxis",
				"RunSpeed({0}) 가 {1} 의 속도 축 최대({2}) 를 넘습니다. 넘는 만큼은 축에서 잘려 "
				"다리는 그대로인 채 몸만 빨라집니다(발 미끄러짐). 더 빠른 클립을 블렌드스페이스에 "
				"한 줄 더 넣거나 RunSpeed 를 낮추십시오."),
				FText::AsNumber(RunSpeed), FText::FromString(Locomotion->GetName()),
				FText::AsNumber(SpeedAxis.Max)));
			Result = EDataValidationResult::Invalid;
		}

		// 샘플에서 먼 속도는 보폭이 어긋난다. 두 샘플 사이가 의도일 수 있어 경고만 한다.
		auto HasSampleNear = [Locomotion](float Speed)
		{
			const float Tolerance = FMath::Max(Speed * 0.15f, 10.0f);
			for (const FBlendSample& Sample : Locomotion->GetBlendSamples())
			{
				if (FMath::Abs(static_cast<float>(Sample.SampleValue.Y) - Speed) <= Tolerance)
				{
					return true;
				}
			}

			return false;
		};

		if (!HasSampleNear(WalkSpeed))
		{
			Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_WalkSpeedOffSample",
				"WalkSpeed({0}) 근처(±15%) 에 {1} 의 샘플이 없습니다. 이 속도에서는 옆 샘플이 섞여 "
				"보폭이 실제 이동과 어긋납니다."),
				FText::AsNumber(WalkSpeed), FText::FromString(Locomotion->GetName())));
		}

		if (!HasSampleNear(RunSpeed))
		{
			Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_RunSpeedOffSample",
				"RunSpeed({0}) 근처(±15%) 에 {1} 의 샘플이 없습니다. 두 샘플 사이를 의도한 값이면 "
				"그대로 두십시오."),
				FText::AsNumber(RunSpeed), FText::FromString(Locomotion->GetName())));
		}
	}
}

void UVBEnemyConfig::ValidateApproachBudget(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	// 접근 상한이 정상 접근 시간보다 짧으면 모든 적이 붙기 전에 토큰을 뺏긴다(에러).
	// 식은 이상화된 값이라 1.25배 안쪽은 빠듯하다는 경고만 한다.
	const float NormalApproachSeconds = GetNormalApproachSeconds();
	if (MaxApproachHoldSeconds <= NormalApproachSeconds)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_ApproachBudgetTooShort",
			"MaxApproachHoldSeconds({0}) 가 정상 접근 시간({1}초) 보다 짧습니다. "
			"시야 끝({2})에서 사거리({3})까지 뛰기 {4} / 걷기 {5} 로 오는 데 그만큼 걸리므로, "
			"이 아키타입의 적은 붙기 전에 매번 토큰을 뺏깁니다(로그는 정상 재배분으로 찍힙니다)."),
			FText::AsNumber(MaxApproachHoldSeconds), FText::AsNumber(NormalApproachSeconds),
			FText::AsNumber(SightRadius), FText::AsNumber(AttackCommitDistance),
			FText::AsNumber(RunSpeed), FText::AsNumber(WalkSpeed)));
		Result = EDataValidationResult::Invalid;
	}
	else if (MaxApproachHoldSeconds <= NormalApproachSeconds * 1.25f)
	{
		Context.AddWarning(FText::Format(LOCTEXT("VBEnemy_ApproachBudgetTight",
			"MaxApproachHoldSeconds({0}) 가 정상 접근 시간({1}초) 의 1.25배 안쪽입니다. "
			"이 식은 직선·즉시가속·정지한 대상을 가정하므로 실제 접근은 반드시 더 걸립니다 - "
			"내비 우회나 후퇴하는 플레이어에서 토큰 회수가 납니다."),
			FText::AsNumber(MaxApproachHoldSeconds), FText::AsNumber(NormalApproachSeconds)));
	}
}

void UVBEnemyConfig::ValidateDeathTiming(FDataValidationContext& Context, EDataValidationResult& Result) const
{
	// 사망 몽타주 + 디졸브가 정리 지연 안에 들어야 시체가 연출 도중에 사라지지 않는다(사망 재생배율은 1.0 고정).
	// ResolveConfig 를 거친다 - 이 필드는 보통 비어 있고 프로젝트 기본값이 걸린다.
	const UVBDeathDissolveConfig* EffectiveDissolve = UVBDeathDissolveSettings::ResolveConfig(DeathDissolve);
	const float DissolveSeconds = EffectiveDissolve ? FMath::Max(0.0f, EffectiveDissolve->Duration) : 0.0f;

	// 엎드린 죽음도 같은 정리 지연 안에 들어야 한다(빈 칸은 엎드린 채 래그돌이라 정상).
	TArray<const UAnimMontage*> TimedDeaths;
	for (const TObjectPtr<UAnimMontage>& DeathMontage : DeathMontages)
	{
		TimedDeaths.Add(DeathMontage.Get());
	}

	if (DownedDeathMontage)
	{
		TimedDeaths.Add(DownedDeathMontage);
	}

	for (const UAnimMontage* DeathMontage : TimedDeaths)
	{
		if (!DeathMontage)
		{
			Context.AddError(LOCTEXT("VBEnemy_DeathMontageNull",
				"DeathMontages 에 빈 칸이 있습니다. 빈 칸이 뽑히면 그 죽음만 연출 없이 래그돌로 떨어져 «가끔 죽는 모션이 안 나온다» 가 됩니다."));
			Result = EDataValidationResult::Invalid;
			continue;
		}

		const float MontageLength = DeathMontage->GetPlayLength();
		if (MontageLength + DissolveSeconds > DeathCleanupDelay)
		{
			Context.AddError(FText::Format(LOCTEXT("VBEnemy_DeathCleanupTooShort",
				"DeathCleanupDelay({0}초) 가 사망 몽타주 {1}({2}초) + 디졸브({3}초) 보다 짧습니다. "
				"시체가 연출 도중에 사라집니다 - 정리 지연을 그 둘의 합보다 길게 잡으세요."),
				FText::AsNumber(DeathCleanupDelay), FText::FromString(DeathMontage->GetName()),
				FText::AsNumber(MontageLength), FText::AsNumber(DissolveSeconds)));
			Result = EDataValidationResult::Invalid;
		}
	}

	// 몽타주가 없으면 위 루프가 안 돌지만 디졸브는 돌므로 따로 잰다.
	if (DeathMontages.Num() == 0 && DissolveSeconds > DeathCleanupDelay)
	{
		Context.AddError(FText::Format(LOCTEXT("VBEnemy_DeathCleanupShorterThanDissolve",
			"DeathCleanupDelay({0}초) 가 디졸브 길이({1}초) 보다 짧습니다. "
			"몸이 다 사라지기 전에 액터가 제거되어 시체가 툭 없어집니다."),
			FText::AsNumber(DeathCleanupDelay), FText::AsNumber(DissolveSeconds)));
		Result = EDataValidationResult::Invalid;
	}
}

#undef LOCTEXT_NAMESPACE
#endif // WITH_EDITOR
