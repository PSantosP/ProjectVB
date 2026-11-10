// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
class AActor;
namespace VBPlayBotDamageEvidence
{
	// 플레이어와 PlayerState 별칭을 같은 규칙으로 대조하고 자해는 제외한다.
	inline bool IsFromPlayer(const AActor* Instigator, const AActor* Victim, const AActor* Player, const AActor* PlayerState)
	{
		return Instigator && Player && Victim != Player && (Instigator == Player || Instigator == PlayerState);
	}
}
