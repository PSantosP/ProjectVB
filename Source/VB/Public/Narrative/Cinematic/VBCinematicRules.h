#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "VBCinematicRules.generated.h"

class UVBCinematicDefinition;

USTRUCT(BlueprintType, meta=(DisplayName="시네마틱 재생"))
struct VB_API FVBEffect_PlayCinematic : public FVBEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Cinematic")
	TObjectPtr<UVBCinematicDefinition> Definition;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Cinematic")
	FName PlayerBindingTag = TEXT("Player");

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Cinematic",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnCompletedEffects;

	// 복귀 결과와 일회 사실을 알린 뒤 저장 잠금 밖에서 적용한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Cinematic",
		meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> OnHandoffEffects;

	virtual void Apply(const FVBRuleContext& Context) const override;
	// 장면은 카메라와 입력을 가져간다 - 플레이어가 준비되기 전(생성, 복원, 페이드)에 걸리면 감독이 거절하고 그 한 번이 사라진다.
	virtual bool NeedsReadyPlayer() const override { return true; }
	// 받아들여지지 않으면 바로, 받아들여지면 감독이 장면을 닫을 때 결과를 알린다.
	virtual bool ReportsDeferredResult() const override { return true; }
	virtual const UObject* GetPlayedScene() const override;
};

USTRUCT(BlueprintType, meta=(DisplayName="시네마틱 조종 복귀 완료"))
struct VB_API FVBCondition_CinematicCompleted : public FVBCondition
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Cinematic")
	TObjectPtr<UVBCinematicDefinition> Definition;

	virtual bool Evaluate(const FVBRuleContext& Context) const override;
	// 완료 영수증은 감독의 메모리에만 있다 - 저장되지 않는다.
	virtual const UObject* GetTransientSceneRead() const override;
};
