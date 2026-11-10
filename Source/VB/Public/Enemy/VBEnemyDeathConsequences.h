// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Templates/SubclassOf.h"

class AActor;
class UGameplayEffect;
class UVBEnemyConfig;
struct FVBDeathFacts;

/**
 * 적 하나가 죽었을 때 남는 결과(보상). 상태가 없다 - 부르는 쪽이 이번 죽음의 정보와 적 설정을 전달한다. 서버에서만 부른다.
 * 부르는 쪽은 AVBEnemyBase 의 죽음 단계(IVBDeathSteps) 하나다: 명성은 BeforeDeathTag, 처치 알림은 AfterDeathTag.
 * 일반 알림(모든 캐릭터의 죽음, 버스 Message.Death)과 다르다 - 여기의 처치 사건은 퀘스트 규칙 입력이라 플레이어가 죽였을 때만 나간다.
 */
namespace VBEnemyDeathConsequences
{
	// 죽인 플레이어에게 기본 사망 명성을 건다. 처형, 정화(Facts.bMedical)는 능력이 이미 처리했으므로 건너뛴다. 빠지는 이유는 로그로 남긴다.
	VB_API void ApplyDefaultReputation(AActor& Enemy, const FVBDeathFacts& Facts,
		const UVBEnemyConfig& Config, TSubclassOf<UGameplayEffect> ReputationEffect);
	// 플레이어가 죽였을 때만: 처치 사건(Event.Enemy.Killed) 다음 처치 효과(Config.OnKilledEffects). 이 순서가 계약이다.
	VB_API void AnnounceKill(AActor& Enemy, const FVBDeathFacts& Facts, const UVBEnemyConfig& Config);
}
