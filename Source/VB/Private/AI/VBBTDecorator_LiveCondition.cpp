// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBBTDecorator_LiveCondition.h"

#include "BehaviorTree/BehaviorTreeComponent.h"

UVBBTDecorator_LiveCondition::UVBBTDecorator_LiveCondition()
{
	// 없으면 TickNode 가 불리지 않는다. 자식은 상속만으로 플래그가 유효해 다시 부를 필요가 없다.
	INIT_DECORATOR_NODE_NOTIFY_FLAGS();
}

void UVBBTDecorator_LiveCondition::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	// 현재 값으로 시작한다. false 로 두면 첫 틱에 '변했다'로 읽혀 방금 시작한 분기를 끊는다.
	CastInstanceNodeMemory<FVBLiveConditionMemory>(NodeMemory)->bLastValue =
		CalculateRawConditionValue(OwnerComp, NodeMemory);
}

void UVBBTDecorator_LiveCondition::TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	FVBLiveConditionMemory* Memory = CastInstanceNodeMemory<FVBLiveConditionMemory>(NodeMemory);
	const bool bValue = CalculateRawConditionValue(OwnerComp, NodeMemory);
	if (bValue == Memory->bLastValue)
	{
		return;
	}

	Memory->bLastValue = bValue;

	// 어느 방향이든 알린다. 무엇을 끊고 여는지는 FlowAbortMode 가 정한다.
	OwnerComp.RequestExecution(this);
}

uint16 UVBBTDecorator_LiveCondition::GetInstanceMemorySize() const
{
	return sizeof(FVBLiveConditionMemory);
}

void UVBBTDecorator_LiveCondition::InitializeMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryInit::Type InitType) const
{
	InitializeNodeMemory<FVBLiveConditionMemory>(NodeMemory, InitType);
}

void UVBBTDecorator_LiveCondition::CleanupMemory(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
	EBTMemoryClear::Type CleanupType) const
{
	CleanupNodeMemory<FVBLiveConditionMemory>(NodeMemory, CleanupType);
}
