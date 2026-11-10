#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"

// 저작된 적 속도와 현재 메시 배율을 CMC 입력 속도로 바꾸는 상태 없는 함수들.
// 메시는 적 캐릭터가 가지고, 돌려받은 속도를 자기 CMC 에 건다.
namespace VBEnemyMovementRules
{
	VB_API float CalculateGaitSpeed(EVBGait Gait, float WalkSpeed, float RunSpeed, float StrideScale);
}
