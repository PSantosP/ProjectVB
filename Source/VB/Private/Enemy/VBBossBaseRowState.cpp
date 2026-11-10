// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Enemy/VBBossBaseRowState.h"

#include "Data/VBBossConfig.h"
#include "Enemy/VBBossAttackRules.h"

void FVBBossRowState::Reset()
{
	HeldRowPhase = INDEX_NONE;
	HeldRow = INDEX_NONE;
}

int32 FVBBossRowState::Choose(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase, const FVBBossSituationFacts& Facts, double Now,
	TFunctionRef<bool(const FVBBossAttackRow&)> CanActivate, float Random01)
{
	Reset();
	TArray<double> ReadyAt;
	ReadyAt.Reserve(Rows.Num());
	for (int32 Index = 0; Index < Rows.Num(); ++Index)
	{
		ReadyAt.Add(RowCooldownReadyAt.FindRef(FIntPoint(Phase, Index)));
	}

	const int32 Chosen = VBBossAttackRules::ChooseRow(Rows, Facts, ReadyAt, Now, CanActivate, Random01);
	if (Chosen != INDEX_NONE)
	{
		HeldRowPhase = Phase;
		HeldRow = Chosen;
	}

	return Chosen;
}

const FVBBossAttackRow* FVBBossRowState::FindHeldRow(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase) const
{
	return HeldRowPhase == Phase && Rows.IsValidIndex(HeldRow) ? &Rows[HeldRow] : nullptr;
}

void FVBBossRowState::StartHeldRowCooldown(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase, double Now)
{
	if (const FVBBossAttackRow* Row = FindHeldRow(Rows, Phase))
	{
		RowCooldownReadyAt.Add(FIntPoint(Phase, HeldRow), Now + Row->CooldownSeconds);
	}
}

void FVBBossRowState::RecordDamage(double Now, double BurstSeconds)
{
	RecentDamageTimes.RemoveAll([Now, BurstSeconds](double Time) { return Now - Time > BurstSeconds; });
	RecentDamageTimes.Add(Now);
}

bool FVBBossRowState::IsAfterHitBurst(double Now, double BurstSeconds, int32 BurstHits) const
{
	return RecentDamageTimes.FilterByPredicate(
		[Now, BurstSeconds](double Time) { return Now - Time <= BurstSeconds; }).Num() >= BurstHits;
}
