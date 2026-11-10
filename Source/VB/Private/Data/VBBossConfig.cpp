// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Data/VBBossConfig.h"

TConstArrayView<FVBBossAttackRow> UVBBossConfig::GetSituationRows(int32 PhaseIndex) const
{
	if (PhaseIndex == 0)
	{
		return FirstPhaseRows;
	}

	return AdditionalPhases.IsValidIndex(PhaseIndex - 1) ? TConstArrayView<FVBBossAttackRow>(AdditionalPhases[PhaseIndex - 1].SituationRows)
		: TConstArrayView<FVBBossAttackRow>();
}

#if WITH_EDITOR
#include "Misc/DataValidation.h"
#define LOCTEXT_NAMESPACE "VBBossConfig"

EDataValidationResult UVBBossConfig::IsDataValid(FDataValidationContext& Context) const
{
	EDataValidationResult Result = Super::IsDataValid(Context);

	// 상황 행: 태그가 비면 그 상황에서 아무 공격도 못 켜고, 무게가 0 이면 영영 안 뽑힌다 - 둘 다 런타임 로그가 없다.
	for (int32 Phase = 0; Phase <= AdditionalPhases.Num(); ++Phase)
	{
		const TConstArrayView<FVBBossAttackRow> Rows = GetSituationRows(Phase);
		for (int32 Index = 0; Index < Rows.Num(); ++Index)
		{
			if (!Rows[Index].InputTag.IsValid() || Rows[Index].CooldownSeconds < 0.0f || Rows[Index].Weight <= 0.0f)
			{
				Context.AddError(FText::Format(LOCTEXT("VBBoss_BadSituationRow",
					"페이즈 {0} 의 상황 행 {1}: InputTag 가 있어야 하고 CooldownSeconds 는 0 이상, Weight 는 0 보다 커야 합니다."), Phase, Index));
				Result = EDataValidationResult::Invalid;
			}
		}
	}

	// 첫 페이즈의 문턱은 1.0 이라 보고 시작한다. 그래서 [0] 은 1.0 미만이어야 하고, 뒤로 갈수록 작아져야 한다.
	float PreviousThreshold = 1.0f;
	for (int32 Index = 0; Index < AdditionalPhases.Num(); ++Index)
	{
		const FVBBossPhase& Phase = AdditionalPhases[Index];

		if (Phase.HealthRatioThreshold <= 0.0f || Phase.HealthRatioThreshold >= PreviousThreshold)
		{
			Context.AddError(FText::Format(LOCTEXT("VBBoss_ThresholdOrder",
				"AdditionalPhases[{0}].HealthRatioThreshold({1}) 는 0 보다 크고 앞 페이즈의 문턱({2}) 보다 작아야 합니다. "
				"아니면 그 페이즈는 절대 들어가지 않거나 앞 페이즈와 같은 순간에 겹쳐 들어갑니다."),
				Index, Phase.HealthRatioThreshold, PreviousThreshold));
			Result = EDataValidationResult::Invalid;
		}

		PreviousThreshold = Phase.HealthRatioThreshold;

		if (!Phase.AttackInputTag.IsValid())
		{
			Context.AddError(FText::Format(LOCTEXT("VBBoss_NoPhaseAttackTag",
				"AdditionalPhases[{0}].AttackInputTag 가 비어 있습니다. 그 페이즈에 들어가면 보스가 영영 공격하지 않습니다 "
				"(UVBEnemyCombatComponent::IsReadyToAttack 이 조용히 false 를 돌려줍니다)."), Index));
			Result = EDataValidationResult::Invalid;
		}
	}

	return Result;
}

#undef LOCTEXT_NAMESPACE
#endif
