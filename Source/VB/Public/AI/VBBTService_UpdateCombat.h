// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "VBBTService_UpdateCombat.generated.h"

class UVBEnemyCombatComponent;

/** 서비스 노드 인스턴스별 메모리. 트리 자산은 공유되므로 전투 컴포넌트 캐시는 여기 둔다. */
struct FVBUpdateCombatMemory
{
	TWeakObjectPtr<UVBEnemyCombatComponent> CombatComponent;
};

/**
 * 코디네이터의 결정을 블랙보드로 옮기는 유일한 통로.
 * 매 주기 블랙보드 TargetActor 를 전투 컴포넌트에 반영하고 역할/슬롯/거리를 되쓰며 AI 포커스를 준다.
 * 이벤트가 아닌 주기적 멱등 조정이라 진입/이탈을 놓치지 않는다.
 */
UCLASS()
class VB_API UVBBTService_UpdateCombat : public UBTService
{
	GENERATED_BODY()

public:
	UVBBTService_UpdateCombat(const FObjectInitializer& ObjectInitializer);

	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual FString GetStaticServiceDescription() const override;

	// 노드 인스턴스 메모리. 0 초기화가 유효한 null 이라는 엔진 매크로에 기대지 않고 생성자/소멸자를 직접 돌린다.
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;

	// 블랙보드 키 선택자. 키 이름은 자산이 정한다.

	// 읽기 전용. 이 서비스에게 전투의 유일한 진실이다.
	UPROPERTY(EditAnywhere, Category="VB|AI")
	FBlackboardKeySelector TargetActorKey;

	UPROPERTY(EditAnywhere, Category="VB|AI")
	FBlackboardKeySelector CombatRoleKey;

	UPROPERTY(EditAnywhere, Category="VB|AI")
	FBlackboardKeySelector SlotLocationKey;

	// 진단 전용(AI Debug 로 본다). 트리는 이 값으로 분기하지 않는다 - 거리 판단은 CombatRole 이 나른다.
	UPROPERTY(EditAnywhere, Category="VB|AI")
	FBlackboardKeySelector DistanceToTargetKey;

	// 접근 태스크가 멈출 거리. 값은 적 자산(공격 시작 거리)이 정하고 이 키는 운반만 한다.
	UPROPERTY(EditAnywhere, Category="VB|AI")
	FBlackboardKeySelector AttackRangeKey;
};
