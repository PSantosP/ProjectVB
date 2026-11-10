// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "VBAN_SendAttackTraceEvent.generated.h"

class UVBSwingImpactProfile;
class UVBSwingCombatProfile;

/**
 * 공격 판정 타이밍 AnimNotify
 * 몽타주 특정 프레임에서 발화 -> GA에 GameplayEvent 전송
 * GA가 WaitGameplayEvent로 수신하여 PerformAttackTrace() 실행
 */
UCLASS(DisplayName = "Send Attack Trace Event")
class VB_API UVBAN_SendAttackTraceEvent : public UAnimNotify
{
	GENERATED_BODY()
public:
	UVBAN_SendAttackTraceEvent();

	// 이 스윙의 임팩트 쉐이크 프로필(옵션). 비우면 공격 leaf 의 DefaultSwingImpactProfile 을 쓴다.
	// 스윙마다 다르므로 노티에 두고, 큐가 UObject 만 싣으므로 자산으로 참조한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBSwingImpactProfile> SwingImpactProfile;

	// 이 스윙의 전투 수치 배율 프로필(옵션). 비우면 무기 leaf 값을 그대로 쓴다.
	// GA 가 서버 경로에서만 읽는다. 쉐이크(클라 연출)와 읽는 쪽이 달라 자산을 나눴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBSwingCombatProfile> SwingCombatProfile;

	virtual void Notify(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	
	// Notify 에디터 표시 이름
	virtual FString GetNotifyName_Implementation() const override;
};
