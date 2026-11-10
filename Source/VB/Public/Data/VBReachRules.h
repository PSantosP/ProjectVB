// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h"   // ECollisionChannel
#include "VBReachRules.generated.h"

/**
 * 그 자리에서 짝 애니가 성립하는가의 조건 - 높이차, 방향각, 시야(GASP CheckForInteractableCharacters 대응).
 * 판정은 UVBAbilitySystemStatics::PassesReachRules 하나이고 처형과 환경 상호작용이 같이 쓴다. 사거리는 따로다.
 */
USTRUCT(BlueprintType)
struct FVBReachRules
{
	GENERATED_BODY()

	// 두 액터의 Z 차이가 이보다 크면 제외(cm). GASP 는 NearlyEqual 의 ErrorTolerance 50 으로 같은 판정을 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach", meta=(ClampMin="0.0"))
	float MaxHeightDifference = 50.0f;

	// 기준 방향에서 야우로 이 각도 안이어야 한다(도). 기준 방향은 부르는 쪽이 정한다.
	// 높이는 MaxHeightDifference 가 따로 보므로 평면 각만 본다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach", meta=(ClampMin="0.0", ClampMax="180.0"))
	float MaxYawAngle = 60.0f;

	// 둘 사이를 캡슐로 쓸어 막히는 것이 있으면 제외한다. 벽 너머 처형을 막는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach")
	bool bRequireLineOfSight = true;

	// 그 캡슐의 치수(cm). GASP 는 r20 / h60 이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach", meta=(ClampMin="1.0"))
	float LineOfSightRadius = 20.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach", meta=(ClampMin="1.0"))
	float LineOfSightHalfHeight = 60.0f;

	// 시야 스윕 채널. 캐릭터가 지나갈 수 있는 길인지 묻는다(GASP 의 CharacterCapsule 프로파일 대응).
	// ECC_Visibility 가 아닌 이유: 캐릭터가 못 지나가는 낮은 난간을 통과시키고 반투명 벽에 걸린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Reach")
	TEnumAsByte<ECollisionChannel> LineOfSightChannel = ECC_Pawn;
};
