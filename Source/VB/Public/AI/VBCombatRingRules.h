#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

// 호출자 순서로 전달받아 참조만 하는 기하 값 - actor, config, combat-lease 를 소유하지 않는다
struct FVBCombatRingMember
{
	FVector Location = FVector::ZeroVector;
	double WaitingDistance = 0.0;
};

// 고른 짝 하나(greedy 수락 순서). MemberIndex 는 호출자 입력의 번호다
struct FVBCombatRingAssignment
{
	int32 MemberIndex = INDEX_NONE;
	int32 SlotIndex = INDEX_NONE;
	FVector Location = FVector::ZeroVector;
};

// 월드 상태 없이 정확한 3D 비용으로 겹치지 않는 멤버/슬롯 짝을 고른다.
// 멤버 모으기, 점 투영, 슬롯 장부 쓰기는 Coordinator 가 한다.
namespace VBCombatRingRules
{
	// 대상에서 대기 자리까지의 거리. 넓히기 전에 float 덧셈을 유지한다 - 저작된 반경 계약이다.
	VB_API double WaitingDistance(float AttackCommitDistance, float WaitingStandoff);
	// 겹치지 않는 멤버와 슬롯의 짝을 가까운 순서로 고른다.
	VB_API TArray<FVBCombatRingAssignment> AssignSlots(const FVector& Center, int32 SlotCount,
		TConstArrayView<FVBCombatRingMember> Members);
}
