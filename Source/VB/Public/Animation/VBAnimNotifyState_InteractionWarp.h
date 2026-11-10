// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AnimNotifyState_MotionWarping.h"
#include "VBAnimNotifyState_InteractionWarp.generated.h"

class UMotionWarpingComponent;
class URootMotionModifier;
struct FPoseSearchBlueprintResult;

/**
 * 처형 상호작용에서 애니가 끝날 때의 정렬 자리를 모션워핑 목표로 넣는 노티파이 스테이트.
 * GASP BP_NotifyState_MMI_MotionWarping 의 C++ 판. 액터는 직접 옮기지 않는다.
 * AnimInstance 가 IVBInteractionAnimInterface 로 결과를 들고 있어야 하며, 없으면 워프 없이 재생한다.
 */
UCLASS(meta = (DisplayName = "VB Interaction Warp"))
class VB_API UVBAnimNotifyState_InteractionWarp : public UAnimNotifyState_MotionWarping
{
	GENERATED_BODY()

public:
	// 재생과 공간 검사가 같은 정렬 원점을 사용한다.
	static bool CalculateAlignedTransform(const FPoseSearchBlueprintResult& Result, float TimeOffset,
		FName AnchorRole, FTransform& OutTransform);

	/** 검색 시점 포즈를 씬 원점으로 고정할 역할. None 이면 GASP 공용 정렬을 쓴다. */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Interaction")
	FName AlignmentAnchorRole = NAME_None;

	/**
	 * 부모가 만든 모디파이어에 우리 콜백을 먼저 건다.
	 * 부모는 이미 바인딩돼 있으면 건너뛰므로 이것이 BP 이벤트를 대체한다.
	 */
	virtual URootMotionModifier* AddRootMotionModifier_Implementation(
		UMotionWarpingComponent* MotionWarpingComp, const UAnimSequenceBase* Animation,
		float StartTime, float EndTime) const override;

private:
	/** 창이 열릴 때 목표를 한 번 계산해 넣는다. */
	UFUNCTION()
	void HandleWarpActivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier);

	/** 끝난 이동을 다음 루트모션 갱신에서 관찰한다. 여기서 액터를 옮기지 않는다. */
	UFUNCTION()
	void HandleWarpDeactivate(UMotionWarpingComponent* MotionWarpingComp, URootMotionModifier* Modifier);
};
