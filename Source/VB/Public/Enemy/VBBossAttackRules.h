#pragma once

#include "CoreMinimal.h"
#include "Containers/ArrayView.h"

struct FVBBossAttackRow;

// 보스가 공격을 계획하는 순간 보는 사실들. 보스가 읽기만 하고 여기서 가지는 것은 없다.
struct FVBBossSituationFacts
{
	bool bTargetBehind = false;
	bool bTargetFar = false;
	bool bAfterHitBurst = false;
};

// 월드 상태 없이 상황 줄이 페이즈 기본 공격을 대신할지 정한다.
// 사실 모으기는 AVBBossBase, 고른 줄 보관과 cooldown 찍기는 FVBBossRowState 가 한다.
namespace VBBossAttackRules
{
	// 2D 판정 - 대상이 보스 정면에서 AngleDegrees 보다 더 벗어났는가.
	VB_API bool IsTargetBehind(const FVector& Forward, const FVector& ToTarget, float AngleDegrees);

	// 상황이 맞고 cooldown 이 지났고 발동할 수 있는 줄 중 하나를 Random01([0,1)) 가중치로 고른다.
	//   INDEX_NONE = 맞는 줄 없음, 페이즈 기본을 쓴다. CooldownReadyAt 은 줄마다의 월드 초이고, 없는 칸은 준비됨으로 친다.
	VB_API int32 ChooseRow(TConstArrayView<FVBBossAttackRow> Rows, const FVBBossSituationFacts& Facts,
		TConstArrayView<double> CooldownReadyAt, double Now, TFunctionRef<bool(const FVBBossAttackRow&)> CanActivate, float Random01);
}
