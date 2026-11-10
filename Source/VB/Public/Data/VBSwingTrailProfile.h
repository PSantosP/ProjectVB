// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBSwingTrailProfile.generated.h"

class UMaterialInterface;
class USoundBase;
class UParticleSystem;

/** 부위별 AnimTrail 자산과 실제 메시 소켓 */
USTRUCT(BlueprintType)
struct VB_API FVBAnimTrailBinding
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail")
	TObjectPtr<UParticleSystem> System;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail")
	FName FirstSocket;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail")
	FName SecondSocket;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail", meta=(ClampMin="0.01"))
	float WidthScale = 1.0f;
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail", meta=(ClampMin="0.01", Units="s"))
	float MaxTailSeconds = 0.5f;

	TArray<FString> GatherValidationErrors() const;
};

/**
 * 휘두름 궤적의 모양새. 날의 위치, 길이는 판정 모양(UVBMeleeTraceShapeSet)을 따른다.
 * 궤적 노티(UVBANS_WeaponTrail)가 스윙별로 덮어쓸 수 있다.
 */
UCLASS(BlueprintType)
class VB_API UVBSwingTrailProfile : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 지정한 부위만 AnimTrail로 대체한다. 나머지 부위는 기존 띠를 쓴다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat|Trail", meta=(Categories="Melee.Shape"))
	TMap<FGameplayTag, FVBAnimTrailBinding> AnimTrails;

	TArray<FString> GatherValidationErrors() const;
#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

	// 궤적 띠 머티리얼 인스턴스(MI_VB_SwingTrail_*). 색, 밝기는 그 인스턴스 파라미터가 홈이다.
	// 정점 규약: 알파 = 남은 수명 비율(1 새 -> 0), UV.X = 나이 비율(0 새 -> 1), UV.Y = 0 날 시작 / 1 날 끝.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UMaterialInterface> TrailMaterial;

	// 공기 왜곡(굴절) 머티리얼 인스턴스. 같은 날 자리를 두 번째 구역으로 한 번 더 그린다. 비우면 안 그린다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<UMaterialInterface> DistortionMaterial;

	// 띠 조각 하나가 사라지기까지(초, 공격자 시간 - 히트스톱에 함께 멈춘다). 짧을수록 날카롭다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.02"))
	float SegmentLifetime = 0.12f;

	// 공기 왜곡이 남아 일렁이는 시간(초). 띠와 같으면 밝은 띠에 덮여 안 보여 따로 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="0.02"))
	float DistortionLifetime = 0.35f;

	// 조각이 살아 있어야 하는 시간 - 그려지는 구역 중 가장 긴 수명.
	float GetMaxSegmentLifetime() const { return DistortionMaterial ? FMath::Max(SegmentLifetime, DistortionLifetime) : SegmentLifetime; }

	// 프레임 사이 날 끝이 이만큼(cm) 움직일 때마다 한 조각씩 채운다. 작을수록 호가 매끈하다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat", meta=(ClampMin="1"))
	float SubstepSpacing = 8.0f;

	// 궤적이 열릴 때 한 번 내는 소리. 변주는 사운드 큐가 맡는다. 비우면 소리 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Combat")
	TObjectPtr<USoundBase> WhooshSound;
};
