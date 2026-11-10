// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

// 메뉴 프레임 제한 요청 하나. 받은 쪽이 들고 있다가 같은 손잡이로 거둔다 - 수를 세는 bool 이 아니다.
struct FVBFrameRateRequestHandle
{
	int32 Id = INDEX_NONE;
	bool IsValid() const { return Id != INDEX_NONE; }
};
