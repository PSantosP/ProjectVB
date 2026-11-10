#pragma once

#include "CoreMinimal.h"

class UPrimitiveComponent;
// 캐릭터에 메시를 새로 만들면 여기 함수를 불러와야 한다.
namespace VBCharacterSurface
{
	// 몸 메시용. 바닥 피 자국이 몸에 묻지 않게
	VB_API void ApplyBodyRules(UPrimitiveComponent& Body);
	// 겉모습 무기 메시용. 피 자국이 안묻고 부딪히지도 않는다.
	VB_API void ApplyVisualRules(UPrimitiveComponent& Visual);
}