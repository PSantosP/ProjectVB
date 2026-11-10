// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

class UActorComponent;

namespace VBServerWrite
{
	// 서버만 쓰는 상태에 이 컴포넌트가 지금 쓸 수 있나. 권한 없는 쪽이면 경고를 남기고 false.
	//   소유자가 없으면 true 다 - 시험과 초기화 경로가 소유자 없이 쓴다.
	VB_API bool IsAllowed(const UActorComponent& Component, const TCHAR* Subject, const TCHAR* What);

	// 소유자가 있고 그 소유자가 서버 쪽인가. 소유자가 없으면 false 라 조용히 건너뛰는 쪽이다.
	VB_API bool HasOwnerAuthority(const UActorComponent& Component);
}
