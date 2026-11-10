// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBThreatTrackerSubsystem.generated.h"

/** 지금 예고 중인 적 하나. HUD 가 화면 밖 화살표를 그리는 데 필요한 것만 든다. */
USTRUCT()
struct FVBThreatEntry
{
	GENERATED_BODY()

	TWeakObjectPtr<AActor> Enemy;

	// 답(Attack.Answer.*).
	FGameplayTag Answer;

	// 답의 색. 큐가 연출 자산에서 읽어 전달한다.
	FLinearColor Color = FLinearColor::White;

	double StartTime = 0.0;
};

/**
 * 클라이언트 쪽 "지금 나를 노리는 예고" 목록. 예고 큐가 등록, 해제하고 HUD 가 매 틱 읽는다.
 * 큐(CDO)와 HUD 가 서로 모르게 하려고 서브시스템에 둔다. 복제 없음, 클라마다 따로 선다.
 */
UCLASS()
class VB_API UVBThreatTrackerSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// 같은 적을 다시 등록하면 갱신이다(중복 행 없음).
	void Register(AActor* Enemy, const FGameplayTag& Answer, const FLinearColor& Color);
	void Unregister(const AActor* Enemy);

	// 살아 있는 항목만 돌려준다. 죽은 약참조는 이 호출이 걷어낸다.
	const TArray<FVBThreatEntry>& GetThreats();

private:
	TArray<FVBThreatEntry> Threats;
};
