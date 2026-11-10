// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "VBANS_AttackTraceWindow.generated.h"

class UVBSwingImpactProfile;
class UVBSwingCombatProfile;

/**
 * 무기 궤적 판정 창(스윙 하나 = 창 하나). 시작에 Event.Montage.AttackTrace, 끝에 AttackTraceEnd 를 보낸다.
 * 시작 태그는 기존 노티와 같은 "닿는 프레임" 신호다. 둘은 InstigatorTags 의 Melee.Window 로 가른다.
 * 스윕은 서버 공격 GA 가 UVBMeleeTraceComponent 로 한다. 이 노티는 알리기만 한다.
 */
UCLASS(DisplayName = "Attack Trace Window")
class VB_API UVBANS_AttackTraceWindow : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBANS_AttackTraceWindow();

	// 판정할 날(모양은 공격자의 UVBMeleeTraceComponent 가 든다). 여러 개면 한 창으로 묶어 표적당 한 번 맞힌다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Melee.Shape"))
	FGameplayTagContainer Shapes;

	// 창 안에서 같은 표적을 다시 맞히는 간격(초, 애니 시간). 0 = 표적당 한 번.
	// 0 보다 크면 반응 이벤트가 타격마다 간다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0"))
	float RehitInterval = 0.0f;

	// 스윙별 프로필. 비우면 공격 leaf 값을 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBSwingImpactProfile> SwingImpactProfile;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBSwingCombatProfile> SwingCombatProfile;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
	virtual void PostLoad() override;

	// 옛 단일 태그를 Shapes 로 옮긴다. PostLoad 가 부르고, 이전 시험을 위해 public.
	void MigrateLegacyShape();

	// 옛 단일 태그. 저장 이름 "Shape" 로 읽기만 한다. 기본값을 일부러 두지 않는다.
	UPROPERTY()
	FGameplayTag Shape_DEPRECATED;
};
