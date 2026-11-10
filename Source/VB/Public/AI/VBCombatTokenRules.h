#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"
#include "AI/VBCombatTypes.h"

// FVBTokenMember - 한 판단 시점의 무리 구성원 하나의 사실 (actor, config, lease 를 갖지 않는다)
struct FVBTokenMember
{
	double Distance = 0.0; // 대상까지의 2D 거리
	double PlannedStart = 0.0; // UVBEnemyCombatComponent::GetAttackStartDistance
	double PlannedRelease = 0.0; // UVBEnemyCombatComponent::GetAttackReleaseDistance - 물러남의 유일한 기준
	double MaxApproachHoldSeconds = 0.0;
	double SecondsSinceGrant = 0.0;
	double DistanceAtGrant = -1.0;
	bool bHeld = false;
	bool bAttackStarted = false;
	bool bWantsCounter = false;
};

// VBCombatTokenRules - 월드 상태 없이 어떤 접근 lease 를 거둘지와 token 을 줄 순서만 정한다
// 사실을 모으고 회수, 부여를 적용하는 것은 Coordinator 의 lease 장부다
namespace VBCombatTokenRules
{
	enum class ERevokeReason : uint8 { ApproachOverrun, MovedAway, ArmoredCounter };
	struct FRevocation
	{
		int32 MemberIndex = INDEX_NONE;
		ERevokeReason Reason = ERevokeReason::ApproachOverrun;
	};

	// SelectRevocations - 아직 접근 중인(공격 전) lease 중 대기열로 돌릴 것을 판단 순서대로 고른다
	// 앞 규칙이 거둔 구성원은 뒤 규칙이 보지 않고, 받을 후보가 없으면 아무것도 거두지 않는다
	VB_API TArray<FRevocation> SelectRevocations(TConstArrayView<FVBTokenMember> Members, TConstArrayView<int32> Candidates);

	// OrderOffers - release 거리 밖으로 물러난 후보(가까운 순), 거둔 구성원(회수 순) 차례이고 아머 반격 요청자는 맨 앞으로 옮긴다
	VB_API TArray<int32> OrderOffers(TConstArrayView<FVBTokenMember> Members, TConstArrayView<int32> Candidates,
		TConstArrayView<int32> Deprioritized);

	// NextRole - token 이 없으면 대기, 가진 쪽은 start 거리 안에서 공격을 시작해 release 거리까지 이어 간다
	VB_API EVBCombatRole NextRole(bool bHeld, EVBCombatRole Previous, double Distance, double PlannedStart, double PlannedRelease);

	// EffectiveCost - 0 이하 코스트는 전원이 공격자가 되므로 바닥을 잡는다
	VB_API int32 EffectiveCost(int32 Cost);

	// PostAttackPause - 다음 부여까지의 숨(초). Roll 은 부르는 쪽이 뽑은 편차다
	VB_API float PostAttackPause(float PauseSeconds, float Roll);
}
