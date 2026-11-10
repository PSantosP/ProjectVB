// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 슈퍼아머 반격 근거 한 줄 - 피격 애니가 켜진 피격 한 번과 그것이 온 타.
// 같은 능력의 같은 타에서 온 판정은 한 번으로 센다(연속 타격이 문턱을 혼자 넘지 않게). Source 가 비면 그 한 번이 한 타.
struct FVBCounterHit
{
	double Time = 0.0;
	TWeakObjectPtr<const UObject> Source; // 피해를 낸 능력 인스턴스
	int32 Swing = INDEX_NONE; // 그 능력의 타 번호(UVBGA_MeleeAttackBase::GetSwingSerial)
};

// 슈퍼아머 반격의 근거. 최근 피격을 타 단위로 세고, 문턱에 닿은 반격을 다음 공격까지 잡아 둔다.
// 태그와 이벤트 구독, 능력을 켤 수 있는지 판정은 UVBEnemyCombatComponent 가 맡는다.
struct VB_API FVBArmoredCounterTracker
{
	// 창 안의 반격 근거 타 수(같은 Source+Swing 은 한 타).
	static int32 CountSwings(const TArray<FVBCounterHit>& Hits, double Now, double WindowSeconds);

	// 창 안 타 수가 문턱 이상이거나, 문턱에 도달한 순간 잡아 둔 반격이 아직 유효하면 true.
	//   잡아 두지 않으면 공격을 고르는 사이 첫 타가 창 밖으로 나가 반격이 보통 공격으로 바뀐다.
	static bool IsWanted(int32 SwingsInWindow, int32 Threshold, double Now, double PendingUntil);

	// 피격 한 대를 기록한다. 창 밖 줄은 걷어내고, 타는 바로 뒤 AttachSwing 이 채운다.
	void RecordHit(double Now, double WindowSeconds);

	// 방금 기록한 줄에 타를 채운다. 그 줄을 기다리는 중이고 같은 시각이면 true. 늦은 이벤트는 옛 줄을 채우지 않는다.
	bool AttachSwing(double Now, const UObject* Source, int32 Swing);

	// 지금 창 안의 타 수.
	int32 CountRecentSwings(double Now, double WindowSeconds) const { return CountSwings(RecentHits, Now, WindowSeconds); }

	// 지금 반격을 원하는 근거가 있으면 true.
	bool WantsCounter(int32 Threshold, double Now, double WindowSeconds) const;

	// 이 시각까지 반격을 잡아 둔다.
	void HoldCounterUntil(double Time) { CounterPendingUntil = Time; }

	int32 GetHitCount() const { return RecentHits.Num(); }

	// 근거를 비운다. 공격이 시작되거나 전투가 끝날 때 부른다.
	void Reset();

private:
	// 창 밖은 기록할 때 걷어낸다.
	TArray<FVBCounterHit> RecentHits;
	// 방금 기록한 줄이 아직 타를 못 받았다.
	bool bAwaitingSwing = false;
	// 잡아 둔 반격의 만료 시각. 0 = 없음.
	double CounterPendingUntil = 0.0;
};
