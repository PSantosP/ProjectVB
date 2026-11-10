// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "GameplayTagContainer.h"
#include "VBANS_WeaponTrail.generated.h"

class UVBSwingTrailProfile;

/**
 * 휘두름 궤적 구간. 판정 창과 시각이 달라 따로 둔다.
 * 치장이라 모든 머신에서 돈다. 궤적 상태는 소유 액터의 UVBSwingTrailComponent 가 든다.
 */
UCLASS(DisplayName = "Weapon Trail")
class VB_API UVBANS_WeaponTrail : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UVBANS_WeaponTrail();

	// 따라 그릴 날(판정 창과 같은 태그). 여러 개면 부위마다 궤적을 그리고 소리는 한 번 낸다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(Categories="Melee.Shape"))
	FGameplayTagContainer Shapes;

	// 이 스윙만의 궤적. 비우면 모양의 기본 궤적(FVBMeleeTraceShapeDef::TrailProfile).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UVBSwingTrailProfile> Profile;

	virtual void NotifyBegin(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation, float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;
	virtual void NotifyEnd(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;
	virtual FString GetNotifyName_Implementation() const override;
	virtual void PostLoad() override;

	// UVBANS_AttackTraceWindow::MigrateLegacyShape 와 같은 이전.
	void MigrateLegacyShape();

	// 옛 단일 태그. 저장 이름 "Shape" 로 읽기만 한다. 기본값 없음.
	UPROPERTY()
	FGameplayTag Shape_DEPRECATED;
};
