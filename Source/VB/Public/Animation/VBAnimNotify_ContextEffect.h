// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotify.h"
#include "GameplayTagContainer.h"
#include "VBAnimNotify_ContextEffect.generated.h"

class UVBContextEffectsLibrary;

/**
 * 애니의 한 프레임에 순간 태그(AnimEffect.*)와 뼈만 적는 표시. 무엇을 틀지는 캐릭터의 UVBContextEffectComponent 표가 정한다.
 * 같은 클립을 여러 캐릭터가 쓰므로 출력을 클립에 박지 않는다.
 */
UCLASS(meta=(DisplayName="VB Context Effect"))
class VB_API UVBAnimNotify_ContextEffect : public UAnimNotify
{
	GENERATED_BODY()

public:
	// 이 순간의 이름(예: AnimEffect.Foot.Plant). 표의 행과 정확히 같아야 맞는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(Categories="AnimEffect"))
	FGameplayTag EffectTag;

	// 위치를 줄 뼈(예: foot_r, hand_l). 비면 메시 위치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	FName Bone;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.0"))
	float VolumeMultiplier = 1.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.01"))
	float PitchMultiplier = 1.0f;

	// 애님 에디터 미리보기 전용 표. 게임에서는 캐릭터의 표를 쓴다.
	UPROPERTY(EditAnywhere, Category="VB|Effects")
	TObjectPtr<UVBContextEffectsLibrary> PreviewLibrary;

	virtual void Notify(USkeletalMeshComponent* MeshComp, UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	// 타임라인에 태그 이름을 보인다.
	virtual FString GetNotifyName_Implementation() const override;
};
