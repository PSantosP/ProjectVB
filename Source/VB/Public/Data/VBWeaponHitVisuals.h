// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "AbilitySystem/VBHitImpact.h"
#include "VBWeaponHitVisuals.generated.h"

class UNiagaraSystem;

/** 때린 부위(Melee.Shape.*)로 고르는 타격 이펙트 한 줄 - 주먹 버스트, 내려찍기 충격처럼 부위가 정하는 모습. */
USTRUCT(BlueprintType)
struct VB_API FVBStrikeVisualRow
{
	GENERATED_BODY()

	// 이 부위로 맞힌 타격에만 나온다. 비우면 저작 오류다(아무 부위는 울렁임 층이 맡는다).
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike", meta=(Categories="Melee.Shape"))
	FGameplayTagContainer Shapes;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike")
	TObjectPtr<UNiagaraSystem> System;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike", meta=(ClampMin="0.01"))
	float Scale = 1.0f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike")
	FLinearColor Color = FLinearColor::White;

	// 이 등급의 타격에만 나온다(공격이 선언한 등급). 비우면 모든 등급 - 마무리 발차기처럼 부위 + 세기로 가를 때 쓴다.
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike")
	TArray<EVBHitImpact> Impacts;

	bool Matches(const FGameplayTag& StrikeShape, EVBHitImpact Impact) const
	{
		return StrikeShape.IsValid() && Shapes.HasTagExact(StrikeShape) && (Impacts.IsEmpty() || Impacts.Contains(Impact));
	}
};

/**
 * 무기 하나의 근접 타격 접점 이펙트: 울렁임, 피 터짐, 베인 자국. 무기 정의가 들고, 타격 큐(UVBGC_MeleeImpact)가 타격 문맥의 무기로 찾는다.
 * 울렁임과 베인 자국은 피 표현 옵션과 무관하게 나오고, 피 터짐만 옵션을 따른다. 큐가 넣는 User 값 이름은 이 클래스의 상수다.
 */
UCLASS(BlueprintType)
class VB_API UVBWeaponHitVisuals : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 이펙트가 받아야 하는 User 값 이름(자산 검사가 같은 이름으로 묻는다).
	static const FName RippleScaleParameter;
	static const FName RippleDurationParameter;
	static const FName RippleColorParameter;
	static const FName CutLineWidthParameter;
	static const FName CutLineLengthParameter;
	static const FName CutLineDurationParameter;
	static const FName CutLineColorParameter;
	static const FName StrikeScaleParameter;
	static const FName StrikeColorParameter;

	// 접점의 울렁임. 비면 없다.
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Ripple")
	TObjectPtr<UNiagaraSystem> RippleSystem;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Ripple", meta=(ClampMin="0.01"))
	float RippleScale = 1.0f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Ripple", meta=(ClampMin="0.01", Units="s"))
	float RippleDuration = 0.25f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Ripple")
	FLinearColor RippleColor = FLinearColor::White;

	// 피 터짐. 피 표현이 끄기면 나오지 않고 대체면 큐의 대체 색을 받는다.
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Blood")
	TObjectPtr<UNiagaraSystem> BloodSystem;

	// 메시 위로 날이 지나간 방향의 짧은 베인 자국(+Z = 날 방향, 맞은 뼈에 붙는다). 메시 표면이 없는 접점에서는 나오지 않는다.
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|CutLine")
	TObjectPtr<UNiagaraSystem> CutLineSystem;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|CutLine", meta=(ClampMin="0.01", Units="cm"))
	float CutLineWidth = 2.0f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|CutLine", meta=(ClampMin="0.01", Units="cm"))
	float CutLineLength = 40.0f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|CutLine", meta=(ClampMin="0.01", Units="s"))
	float CutLineDuration = 0.12f;
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|CutLine")
	FLinearColor CutLineColor = FLinearColor::White;

	// 때린 부위별 이펙트. 맞는 줄은 모두 나오고, 피 표현 옵션과 무관하다.
	UPROPERTY(EditAnywhere, Category="VB|HitVisuals|Strike")
	TArray<FVBStrikeVisualRow> StrikeRows;

	// 저작 문제 - 하나에 한 줄. 이펙트가 큐가 넣는 값을 안 받으면 Niagara 가 말없이 무시하므로 저장 때 잡는다.
	TArray<FText> FindProblems() const;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif
};
