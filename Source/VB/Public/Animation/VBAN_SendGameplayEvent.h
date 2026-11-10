// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "VBAN_SendGameplayEvent.generated.h"

/**
 * 몽타주의 한 시각을 능력에 알린다: 서버에서 소유자 ASC 로 저작한 EventTag 의 게임플레이 이벤트를 보낸다.
 * 듣는 쪽이 정하는 이벤트(던지기의 놓는 시각 등)를 노티 클래스를 늘리지 않고 데이터로 단다. 태그가 비면 아무것도 안 보낸다.
 */
UCLASS(DisplayName = "Send Gameplay Event")
class VB_API UVBAN_SendGameplayEvent : public UAnimNotify
{
	GENERATED_BODY()

public:
	// 보낼 이벤트. 듣는 능력의 WaitGameplayEvent 태그와 같아야 한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Event", meta=(Categories="Event"))
	FGameplayTag EventTag;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
};
