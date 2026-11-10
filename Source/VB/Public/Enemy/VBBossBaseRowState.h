// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FVBBossAttackRow;
struct FVBBossSituationFacts;

// 보스 상황 공격의 고르기 상태(서버). 이번 계획의 행, 행 쿨다운, 몰아 맞음 기록을 맡는다.
// 상황 사실 모으기와 토큰 판단은 AVBBossBase 가, 가중치 고르기 규칙은 VBBossAttackRules 가 맡는다.
struct VB_API FVBBossRowState
{
	// 이번 계획의 행을 잊는다. 쿨다운과 피격 기록은 남긴다.
	void Reset();

	// 이번 계획의 행을 고르고 기억한다. 맞는 행이 없으면 INDEX_NONE 이고 페이즈 기본을 쓴다.
	int32 Choose(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase, const FVBBossSituationFacts& Facts, double Now,
		TFunctionRef<bool(const FVBBossAttackRow&)> CanActivate, float Random01);

	// 이 페이즈에 유효한 고른 행. 페이즈가 바뀌었거나 없으면 null.
	const FVBBossAttackRow* FindHeldRow(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase) const;

	// 고른 행으로 공격이 시작됐다. 그 행의 쿨다운을 건다.
	void StartHeldRowCooldown(TConstArrayView<FVBBossAttackRow> Rows, int32 Phase, double Now);

	// 체력이 깎인 시각을 기록한다. 창 밖 기록은 이때 걷어낸다.
	void RecordDamage(double Now, double BurstSeconds);

	// 창 안에 BurstHits 번 이상 맞았으면 true.
	bool IsAfterHitBurst(double Now, double BurstSeconds, int32 BurstHits) const;

private:
	int32 HeldRowPhase = INDEX_NONE;
	int32 HeldRow = INDEX_NONE;
	// (페이즈, 행) -> 다시 고를 수 있는 월드 시각.
	TMap<FIntPoint, double> RowCooldownReadyAt;
	TArray<double> RecentDamageTimes;
};
