// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBContextEffectsLibrary.generated.h"

class FDataValidationContext; // IsDataValid 인자 - 헤더에서 Misc/DataValidation.h 를 끌지 않는다
class UCameraShakeBase;
class UMaterialInterface;
class UNiagaraSystem;
class USoundAttenuation;
class USoundBase;

/**
 * 애니 속 물리 순간(발 딛음, 땅 내려치기, 쓰러져 바닥에 닿음) 하나가 내는 출력 묶음. 비어 있는 칸은 안 튼다.
 * 하드 참조인 이유: 캐릭터와 함께 올라와 첫 발걸음에 적재 끊김이 없다.
 */
USTRUCT(BlueprintType)
struct FVBContextEffectRow
{
	GENERATED_BODY()

	// 이 행이 답하는 순간. 노티파이 태그와 정확히 같아야 맞는다(부모 태그로 번지지 않는다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	FGameplayTag EffectTag;

	// 이 행을 쓰려면 지금 상황이 가져야 할 태그 전부(예: Surface.Stone). 비면 기본 행. 여러 행이 맞으면 태그가 가장 많은(구체적인) 행.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	FGameplayTagContainer Context;

	// 소리 후보. 틀 때마다 하나를 무작위로 - 같은 발소리가 기계적으로 반복되지 않게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TArray<TObjectPtr<USoundBase>> Sounds;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	// 파티클. 풀에서 꺼내 쓰고 끝나면 엔진이 돌려받는다(ENCPoolMethod::AutoRelease).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TObjectPtr<UNiagaraSystem> Niagara = nullptr;

	// 바닥에 남는 자국(금 간 땅 등). 있으면 그때만 바닥을 트레이스해 위치, 법선을 얻는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TObjectPtr<UMaterialInterface> DecalMaterial = nullptr;

	// 데칼 크기(cm, X = 깊이 투영 거리, Y/Z = 넓이). 보폭 배율을 곱할 수 있다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	FVector DecalSize = FVector(40.0f, 120.0f, 120.0f);

	// 데칼이 보이는 시간(초)과 그 끝의 사라지는 시간(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.0"))
	float DecalLifeSeconds = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.0"))
	float DecalFadeSeconds = 2.0f;

	// 월드 카메라 흔들림. 안쪽 반경 안은 최대, 바깥 반경 밖은 없음.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TSubclassOf<UCameraShakeBase> CameraShake;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.0"))
	float ShakeInnerRadius = 300.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects", meta=(ClampMin="0.0"))
	float ShakeOuterRadius = 1500.0f;

	// 파티클, 데칼 크기, 흔들림 반경에 보폭 배율(VBStrideScale::Resolve)을 곱하나.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	bool bScaleWithBody = true;

	// 출력 칸이 하나라도 있나. 함수 본문은 cpp 에 둔다 - TSubclassOf 의 bool 변환에 완전한 형이 필요하다(PCH 없는 빌드).
	bool HasAnyOutput() const;
};

/**
 * "이펙트 태그 + 상황 태그 -> 출력" 표(Lyra ULyraContextEffectsLibrary 대응). UVBContextEffectComponent 가 여러 개를 순서대로 찾는다.
 * 노티파이는 무슨 순간인지만 말하고, 무엇을 틀지는 캐릭터 쪽 이 표가 정한다.
 */
UCLASS(BlueprintType)
class VB_API UVBContextEffectsLibrary : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Effects")
	TArray<FVBContextEffectRow> Rows;

	// EffectTag 가 정확히 같고 Context 가 Contexts 에 전부 들어 있는 행 중 가장 구체적인 것. 없으면 null.
	const FVBContextEffectRow* FindRow(const FGameplayTag& EffectTag, const FGameplayTagContainer& Contexts) const;

#if WITH_EDITOR
	// 저작 실수를 저장 전에 잡는다: 태그 없는 행, 출력이 하나도 없는 행, 같은 (태그, 상황) 이 두 번 나오는 행.
	virtual EDataValidationResult IsDataValid(FDataValidationContext& Context) const override;
#endif
};
