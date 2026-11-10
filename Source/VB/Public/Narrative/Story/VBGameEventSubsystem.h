// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBGameEventSubsystem.generated.h"

class APlayerState;

/** 일어난 게임 사건 하나. */
USTRUCT(BlueprintType)
struct VB_API FVBGameEvent
{
	GENERATED_BODY()

	// 무슨 일(예: Event.Enemy.Killed, Event.Interaction.Completed, Event.Dialogue.NodeReached).
	UPROPERTY(BlueprintReadOnly, Category="VB|Story")
	FGameplayTag EventTag;

	// 무엇에 대해(예: 죽은 적의 종류 태그, 연 문의 태그). 퀘스트 목표는 이 태그들에 질의를 건다.
	UPROPERTY(BlueprintReadOnly, Category="VB|Story")
	FGameplayTagContainer TargetTags;

	// 일으킨 플레이어. 없으면 세계가 일으킨 일(시간 경과 등).
	UPROPERTY(BlueprintReadOnly, Category="VB|Story")
	TObjectPtr<APlayerState> Instigator = nullptr;

	// 수량(주운 개수 등). 기본 1.
	UPROPERTY(BlueprintReadOnly, Category="VB|Story")
	int32 Count = 1;
};

DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnGameEvent, const FVBGameEvent&);

/**
 * 게임 사건 방송. 생산자는 태그로 방송만 하고, 듣는 쪽(퀘스트 일지 등)은 보낸 쪽을 모른 채 거른다.
 * 서버에서만 방송한다 - 클라 호출은 경고 후 버려 같은 사건이 두 번 세지지 않게 한다.
 */
UCLASS()
class VB_API UVBGameEventSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	static UVBGameEventSubsystem* Get(const UObject* WorldContextObject);

	void Broadcast(const FVBGameEvent& Event);

	FVBOnGameEvent OnGameEvent;
};
