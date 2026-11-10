// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBArmoredCounterTracker.h"

#include "UObject/Object.h"

int32 FVBArmoredCounterTracker::CountSwings(const TArray<FVBCounterHit>& Hits, double Now, double WindowSeconds)
{
	// 같은 능력 + 같은 타는 하나로, 타를 모르는 줄은 각각 한 타로 센다. 배열이 작아 이중 루프로 충분하다.
	int32 Count = 0;
	for (int32 i = 0; i < Hits.Num(); ++i)
	{
		const FVBCounterHit& Hit = Hits[i];
		if (Now - Hit.Time > WindowSeconds)
		{
			continue;
		}

		bool bSeen = false;
		if (Hit.Swing != INDEX_NONE && Hit.Source.IsValid())
		{
			for (int32 j = 0; j < i && !bSeen; ++j)
			{
				const FVBCounterHit& Prev = Hits[j];
				bSeen = Now - Prev.Time <= WindowSeconds && Prev.Swing == Hit.Swing && Prev.Source == Hit.Source;
			}
		}

		if (!bSeen)
		{
			++Count;
		}
	}

	return Count;
}

bool FVBArmoredCounterTracker::IsWanted(int32 SwingsInWindow, int32 Threshold, double Now, double PendingUntil)
{
	return Threshold > 0 && (SwingsInWindow >= Threshold || Now < PendingUntil);
}

void FVBArmoredCounterTracker::RecordHit(double Now, double WindowSeconds)
{
	RecentHits.RemoveAll([Now, WindowSeconds](const FVBCounterHit& Hit) { return Now - Hit.Time > WindowSeconds; });
	FVBCounterHit& Hit = RecentHits.AddDefaulted_GetRef();
	Hit.Time = Now;
	bAwaitingSwing = true;
}

bool FVBArmoredCounterTracker::AttachSwing(double Now, const UObject* Source, int32 Swing)
{
	if (!bAwaitingSwing || RecentHits.IsEmpty())
	{
		return false;
	}

	bAwaitingSwing = false;
	FVBCounterHit& Hit = RecentHits.Last();
	// 태그 에지와 이벤트는 같은 시각이다. 다르면 이 줄의 이벤트가 아니므로 비운 채 둔다.
	if (Hit.Time != Now)
	{
		return false;
	}

	Hit.Source = Source;
	Hit.Swing = Swing;
	return true;
}

bool FVBArmoredCounterTracker::WantsCounter(int32 Threshold, double Now, double WindowSeconds) const
{
	return IsWanted(CountRecentSwings(Now, WindowSeconds), Threshold, Now, CounterPendingUntil);
}

void FVBArmoredCounterTracker::Reset()
{
	RecentHits.Reset();
	bAwaitingSwing = false;
	CounterPendingUntil = 0.0;
}
