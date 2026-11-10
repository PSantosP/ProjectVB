// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "VBTestIniTag.h"

// 실제 어빌리티를 대신하지 않는 시험 어빌리티 double 의 정체 tag. UVBGameplayAbility 는 빈 asset tag 를 거절하므로 하나가 필요하다.
//   Ability.Test 는 어떤 combat rule 줄도 이름 짓지 않고 State.Dead 의 root Ability 칸만 닿는다.
//   다른 시험 tag 처럼 Config/Tags/VBTests.ini 에 등록한다 - 이 모듈의 native tag 는 NoNativeTagsInTestModule 이 막는다.
namespace VBTestAbilityTags
{
	inline constexpr FVBTestIniTag AnyAbility{TEXT("Ability.Test")};
}
