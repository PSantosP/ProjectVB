// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Interaction/VBElevatorRules.h"
#include "Engine/World.h"
#include "Interaction/VBElevator.h"
#include "VB/VB.h"

void FVBEffect_SendElevator::Apply(const FVBRuleContext& Context) const
{
	// 불러오지 않는다 - 올라와 있는 이 월드의 승강기만 움직인다(다른 월드, 에디터 월드의 것을 잡지 않게).
	AVBElevator* Target = Elevator.Get();
	if (!Target || !Context.World || Target->GetWorld() != Context.World)
	{
		VB_LOG(Warning, "승강기 보내기: 승강기 %s 가 이 월드에 없다 - 아무것도 안 한다", *Elevator.ToString());
		return;
	}

	if (!Target->HasAuthority())
	{
		return;
	}

	Target->RequestStop(bUp);
}
