// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBHitDistortionConfig.generated.h"

class UNiagaraSystem;

// 타격 자리의 공간 왜곡 한 종류. 화면 이동량은 1920px, FOV 90 기준 Strength x 848 px 이다.
USTRUCT(BlueprintType)
struct FVBHitDistortionProfile
{
	GENERATED_BODY()

	// 굴절만 그리는 이펙트(NS_VB_HitDistortion). 비우면 이 등급은 왜곡을 내지 않는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<UNiagaraSystem> System = nullptr;

	// 정점의 화면 오프셋(2D Offset 굴절 값). 0.025 = 1920px 에서 약 21px.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.001", ClampMax="0.1"))
	float Strength = 0.025f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="1", Units="cm"))
	float RadiusCm = 25.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.01", ClampMax="0.5", Units="s"))
	float Lifetime = 0.12f;

	// 왜곡이 가장 센 시각. Lifetime 보다 짧아야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0.001", Units="s"))
	float PeakTime = 0.03f;

	// +1 = 바깥으로 밀어내는 물결, -1 = 안으로 빨아들임.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="-1", ClampMax="1"))
	float Direction = 1.0f;
};

/**
 * 타격 자리 공간 왜곡의 데이터. 공격이 선언한 등급(Light/Heavy/Knockdown/Launch)마다 프로필을 고른다.
 * 피격 플래시와 같은 자리, 같은 조건(진짜 맞은 타격, 플래시 컴포넌트가 있는 캐릭터)에서 UVBHitFlashComponent 가 낸다.
 * 프로젝트 기본은 UVBHitFlashSettings::DistortionConfig.
 */
UCLASS(BlueprintType)
class VB_API UVBHitDistortionConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 등급에 맞는 프로필. 없으면 DefaultProfile.
	const FVBHitDistortionProfile& ResolveProfile(EVBHitImpact Impact) const;

	// 프로필 하나의 값 검사. 이펙트가 받는 User 파라미터까지 본다. 틀리면 false 와 이유.
	static bool ValidateProfile(const FVBHitDistortionProfile& Profile, FString& OutError);

	// 이펙트가 받는 float User 파라미터 이름. 이펙트 쪽 이름과 같이 고친다.
	static const FName StrengthParameter;
	static const FName RadiusParameter;
	static const FName LifetimeParameter;
	static const FName PeakTimeParameter;
	static const FName DirectionParameter;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FVBHitDistortionProfile DefaultProfile;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TMap<EVBHitImpact, FVBHitDistortionProfile> ByImpact;
};
