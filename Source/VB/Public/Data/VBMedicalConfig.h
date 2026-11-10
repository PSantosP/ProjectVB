// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameplayEffect.h"
#include "Engine/DataAsset.h"
#include "Data/VBReachRules.h"   // FVBReachRules - 값으로 들고 있다
#include "VBMedicalConfig.generated.h"


/** 의료 Ability 설정 - 사거리, 회복량, 평판, 처형 대상 조건. */
UCLASS()
class VB_API UVBMedicalConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()
	
public:
	// 의료 사거리
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	float MedicalRange = 300.0f;
	
	// 기본 의료 행위량 (SetByCaller에 전달하는 값)
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	float BaseMedicalAmount = 30.0f;
	
	// 처치 시 명성 변동량
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	float ReputationChangeOnMedical = 10.0f;
	
	// 빈사 문턱은 UVBFinisherWindowConfig::HealthThreshold, 탐색 채널은 UVBTargetLockConfig::SearchTraceChannel 에 있다.
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	TSubclassOf<UGameplayEffect> MedicalDamageEffectClass;
	
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	TSubclassOf<UGameplayEffect> ReputationEffectClass;
	
	// 이 행위의 피해 스펙에 다는 표시(Effect.Execute / Effect.Purify). 처형 창이 켜진 적은 이 표시가 있는 피해로만 죽는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	FGameplayTag MedicalActionTag;

	// 처형 대상의 자리 조건(FVBReachRules).
	UPROPERTY(EditDefaultsOnly, Category="VB|Medical")
	FVBReachRules FinisherTargetRules;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

};
