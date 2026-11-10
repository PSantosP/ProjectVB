// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Data/VBDeathDissolveSettings.h"

#include "Data/VBDeathDissolveConfig.h"

UVBDeathDissolveConfig* UVBDeathDissolveSettings::ResolveConfig(UVBDeathDissolveConfig* Explicit)
{
	// 명시된 것이 이긴다. 적 설정의 DeathDissolve 는 이 적만 다르게 하겠다는 뜻이다.
	if (Explicit)
	{
		return Explicit;
	}

	// 아무도 안 정했으면 프로젝트 기본. 그것도 비면 nullptr 이고 연출이 꺼진다.
	return GetDefault<UVBDeathDissolveSettings>()->DefaultConfig.LoadSynchronous();
}
