#pragma once

#include "CoreMinimal.h"

class AActor;

// 애니메이션 고유 거리와 지면 거리를 잇는 배율. 메시는 균일 배율이라고 보고 X 만 읽는다.
namespace VBStrideScale
{
	// 0, 음수, NaN 은 1 로 바꾼다.
	VB_API float Sanitize(float ScaleX);
	// 캐릭터의 메시 배율. 캐릭터가 아니거나 메시가 없으면 1 이다.
	VB_API float Resolve(const AActor* Actor);
}
