// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "VBPlayBotScenario_Interaction.generated.h"

// dynamic 완료 delegate 는 리플렉션된 수신자가 필요하다. 시나리오는 묶여 있는 동안만 이 관찰자를 root 에 둔다.
UCLASS()
class UVBPlayBotInteractionObserver : public UObject
{
	GENERATED_BODY()
public:
	TWeakObjectPtr<AActor> ExpectedPlayer;
	int32 Completions = 0;
	UFUNCTION()
	void OnCompleted(AActor* Player) { if (Player == ExpectedPlayer.Get()) ++Completions; }
};
