#include "AI/VBCombatTokenRules.h"

namespace VBCombatTokenRules
{
	TArray<FRevocation> SelectRevocations(TConstArrayView<FVBTokenMember> Members, TConstArrayView<int32> Candidates)
	{
		TArray<FRevocation> Revocations;
		// 토큰을 받을 후보가 없으면 회수해도 같은 멤버가 다시 받는다 - 무의미한 반복과 그 로그를 막는다.
		if (Candidates.IsEmpty())
		{
			return Revocations;
		}

		TArray<bool> bApproaching;
		bApproaching.SetNumUninitialized(Members.Num());
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			bApproaching[Index] = Members[Index].bHeld && !Members[Index].bAttackStarted;
		}

		auto Revoke = [&](int32 Index, ERevokeReason Reason)
		{
			Revocations.Add({Index, Reason});
			bApproaching[Index] = false;
		};

		// 접근 구간 초과 - 그 시간 안에 사거리에 못 붙었다.
		for (int32 Index = 0; Index < Members.Num(); ++Index)
		{
			if (bApproaching[Index] && Members[Index].SecondsSinceGrant > Members[Index].MaxApproachHoldSeconds)
			{
				Revoke(Index, ERevokeReason::ApproachOverrun);
			}
		}

		// 위치 기준 - 사거리 안 후보가 있을 때, 놓는 거리 밖이고 부여 때보다 멀어진 보유자의 토큰을 옮긴다.
		const bool bAnyCandidateInRange = Candidates.ContainsByPredicate([&Members](int32 Index)
		{
			return Members[Index].Distance <= Members[Index].PlannedStart;
		});
		if (bAnyCandidateInRange)
		{
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				const FVBTokenMember& Member = Members[Index];
				if (!bApproaching[Index] || Member.Distance <= Member.PlannedRelease)
				{
					continue;
				}
				// 부여 때보다 가까워졌으면 걸어오는 중이다.
				if (Member.DistanceAtGrant >= 0.0 && Member.Distance <= Member.DistanceAtGrant)
				{
					continue;
				}

				Revoke(Index, ERevokeReason::MovedAway);
			}
		}

		// 슈퍼아머 반격 - 요청자가 있으면 아직 공격에 들지 않은 보유자 모두에게서 거둔다.
		const bool bAnyCounterRequest = Candidates.ContainsByPredicate([&Members](int32 Index)
		{
			return Members[Index].bWantsCounter;
		});
		if (bAnyCounterRequest)
		{
			for (int32 Index = 0; Index < Members.Num(); ++Index)
			{
				if (bApproaching[Index])
				{
					Revoke(Index, ERevokeReason::ArmoredCounter);
				}
			}
		}

		return Revocations;
	}

	TArray<int32> OrderOffers(TConstArrayView<FVBTokenMember> Members, TConstArrayView<int32> Candidates,
		TConstArrayView<int32> Deprioritized)
	{
		TArray<int32> Order(Candidates);
		// 물러나는 것이 다음 차례의 조건이다 - 물러난 쪽이 앞, 그중 가까운 쪽.
		Order.Sort([&Members](int32 A, int32 B)
		{
			const FVBTokenMember& MemberA = Members[A];
			const FVBTokenMember& MemberB = Members[B];
			const bool bARetreated = MemberA.Distance > MemberA.PlannedRelease;
			const bool bBRetreated = MemberB.Distance > MemberB.PlannedRelease;
			if (bARetreated != bBRetreated)
			{
				return bARetreated;
			}

			return MemberA.Distance < MemberB.Distance;
		});
		// 거둔 멤버는 다른 후보가 다 받고도 남을 때만 받는다.
		Order.Append(Deprioritized);
		// 맞은 그 적이 반격해야 뜻이 선다 - 요청자를 앞으로, 나머지 순서는 그대로.
		Order.StableSort([&Members](int32 A, int32 B)
		{
			return Members[A].bWantsCounter && !Members[B].bWantsCounter;
		});
		return Order;
	}

	EVBCombatRole NextRole(bool bHeld, EVBCombatRole Previous, double Distance, double PlannedStart, double PlannedRelease)
	{
		if (!bHeld)
		{
			return EVBCombatRole::Surrounder;
		}

		const double Threshold = Previous == EVBCombatRole::Attacker ? PlannedRelease : PlannedStart;
		return Distance <= Threshold ? EVBCombatRole::Attacker : EVBCombatRole::Approaching;
	}

	int32 EffectiveCost(int32 Cost)
	{
		return FMath::Max(1, Cost);
	}

	float PostAttackPause(float PauseSeconds, float Roll)
	{
		return FMath::Max(0.0f, PauseSeconds + Roll);
	}
}
