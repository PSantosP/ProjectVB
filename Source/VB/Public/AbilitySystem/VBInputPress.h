// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"

/**
 * 물리 누름 하나의 기록. UVBAbilitySystemComponent 가 관리하고, 그 누름으로 한 일은 능력이 소유한다.
 * Serial 은 소유 클라가 매기고 서버는 같은 번호로 받는다.
 */
struct FVBInputPress
{
	int32 Serial = INDEX_NONE;
	double PressTime = -1.0; // 이 기계의 월드 시각
	double ReleaseTime = -1.0; // 음수 = 아직 누르는 중
	bool bReleaseRequestAccepted = false; // 서버 입구가 이 누름의 놓음 요청을 받아들였다(물리 놓음 기록과 따로 - 리슨 호스트는 로컬이 먼저 놓는다)

	bool IsValid() const { return Serial != INDEX_NONE; }
	bool IsHeld() const { return IsValid() && ReleaseTime < 0.0; }
};
