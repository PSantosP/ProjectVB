// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Camera/VBCombatFeedbackModifier.h"       // FVBScreenImpactSettings - 화면 층의 값
#include "TimeControl/VBTimeDilationSubsystem.h"   // FVBTimeDilationStep - 슬로우 단계를 그대로 저작한다
#include "VBCombatFeedbackProfile.generated.h"

class UNiagaraSystem;
class FDataValidationContext;

// 큐의 화면과 슬로우를 받을 Pawn을 고른다.
UENUM()
enum class EVBFeedbackRecipient : uint8
{
	Target,
	Instigator
};

// 월드 FX 를 어느 쪽으로 세우나. 불꽃처럼 방향이 있는 파편은 접촉 법선이나 스윙 방향을, 고리처럼 둥근 것은 None 을 쓴다.
UENUM()
enum class EVBFeedbackEffectAlign : uint8
{
	// 월드 회전 0. 둥근 고리, 굴절.
	None,
	// 접촉 법선이 +X. 법선이 없으면 None 으로 떨어진다.
	ContactNormal,
	// 사건 방향(공격 = 스윙 방향)이 +X. 방향이 없으면 ContactNormal, 그것도 없으면 None.
	EventDirection
};

USTRUCT(BlueprintType)
struct FVBCombatFeedbackEffect
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	TObjectPtr<UNiagaraSystem> System;

	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="0.01"))
	float Scale = 1.0f;

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	EVBFeedbackEffectAlign Align = EVBFeedbackEffectAlign::None;

	// 시스템의 User 파라미터(이름에 User. 를 붙인다). 같은 시스템을 사건마다 다르게 쓴다. 비우면 시스템 기본값.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	TMap<FName, float> FloatParameters;

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	TMap<FName, int32> IntParameters;

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	TMap<FName, FLinearColor> ColorParameters;
};

/**
 * 한 전투 사건(패링, 가드, 강타...)의 연출 - 월드 Niagara, 로컬 슬로우, 로컬 화면 층. 비운 층은 빠진다.
 * 큐 파라미터가 UObject 만 실을 수 있어 자산이다. 실행은 UVBGC_CombatFeedback::PlayFeedback.
 */
UCLASS(BlueprintType)
class VB_API UVBCombatFeedbackProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback|World", meta=(TitleProperty="System"))
	TArray<FVBCombatFeedbackEffect> WorldEffects;

	// 앞에서부터 차례로 걸리고 마지막이 끝나면 정상 속도. 비우면 슬로우 없음.
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback|Time", meta=(TitleProperty="x{Scale}  {RealDuration}초"))
	TArray<FVBTimeDilationStep> SlowMotionSteps;

	// 화면 층을 쓰나. 끄면 월드 FX, 슬로우만 난다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback|Screen")
	bool bScreenImpact = true;

	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback|Screen")
	EVBFeedbackRecipient ScreenRecipient = EVBFeedbackRecipient::Target;

	UPROPERTY(EditDefaultsOnly, Category="VB|Feedback|Screen", meta=(EditCondition="bScreenImpact"))
	FVBScreenImpactSettings Screen;

#if WITH_EDITOR
	// 세 층이 다 비면 배선 누락과 구별이 안 되므로 저장 시점에 잡는다.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
