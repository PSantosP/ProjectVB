// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "VBHitReceivedMessage.generated.h"

/**
 * 캐릭터가 타격을 받았다는 알림(Message.Combat.HitReceived, 서버). 가드 판정 뒤에 막기, 패링, 슈퍼아머 포함해 보내고, 회피 무적은 안 보낸다.
 * 동기로만 건넨다 - 받는 쪽은 보관하지 않는다.
 */
USTRUCT(BlueprintType)
struct VB_API FVBHitReceivedMessage
{
	GENERATED_BODY()

	// 맞은 캐릭터 = 체력 세트를 가진 ASC 의 아바타.
	UPROPERTY(BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<AActor> Victim = nullptr;
};
