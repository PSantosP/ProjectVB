// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GenericTeamAgentInterface.h"
#include "VBTeamTypes.generated.h"

/**
 * 세력 명부. 값은 곧 엔진 FGenericTeamId 다.
 * 적대 판정은 엔진 기본 규칙(같은 ID = Friendly, 다른 ID = Hostile, NoTeam = Neutral)을 쓴다.
 */
UENUM(BlueprintType)
enum class EVBTeam : uint8
{
	// 255 = NoTeam. 모두에게 Neutral 이라 AI 지각에 안 보인다(소품/장식 전용).
	None = 255 UMETA(DisplayName = "None (소속 없음 - AI 에게 안 보임)"),

	Player = 0 UMETA(DisplayName = "Player"),
	Infected = 1 UMETA(DisplayName = "Infected"),
};

namespace VBTeam
{
	// EVBTeam -> 엔진 팀 ID. 변환 통로를 하나로 둔다.
	FORCEINLINE FGenericTeamId ToGenericId(EVBTeam Team)
	{
		return FGenericTeamId(static_cast<uint8>(Team));
	}
}
