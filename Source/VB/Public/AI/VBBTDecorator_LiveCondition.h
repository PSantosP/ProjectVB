// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "VBBTDecorator_LiveCondition.generated.h"

/** 관찰형 데코레이터의 인스턴스별 메모리. 직전 프레임의 조건값 하나뿐이다. */
struct FVBLiveConditionMemory
{
	bool bLastValue = false;
};

/**
 * 매 프레임 원본에 다시 묻고 답이 바뀌면 재탐색을 요청하는 관찰형 데코레이터의 뼈대.
 * 무엇을 묻는지는 자식의 CalculateRawConditionValue 가 정하고, 여기는 언제 알리는가만 갖는다.
 * 쓰는 노드는 FlowAbortMode 를 설정해야 분기를 끊을 수 있다.
 */
UCLASS(Abstract)
class VB_API UVBBTDecorator_LiveCondition : public UBTDecorator
{
	GENERATED_BODY()

public:
	UVBBTDecorator_LiveCondition();

protected:
	// 조건이 바뀌는 순간 트리에 재탐색을 요청한다. 판정은 자식이 한다.
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 트리 자산은 공유되므로 직전 값은 인스턴스 메모리에 둔다.
	virtual uint16 GetInstanceMemorySize() const override;
	virtual void InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryInit::Type InitType) const override;
	virtual void CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, EBTMemoryClear::Type CleanupType) const override;
};
