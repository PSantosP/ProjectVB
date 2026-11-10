// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Curves/CurveFloat.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBHitFlashConfig.generated.h"

class UMaterialInterface;

/** 히트 플래시 한 종류의 표현 묶음(머티리얼 + 길이 + 색/강도/감쇠). TagOverrides 의 값 타입이다. */
USTRUCT(BlueprintType)
struct FVBHitFlashProfile
{
	GENERATED_BODY()

	// 메시에 덮을 오버레이 머티리얼(MI). 미할당이면 이 프로필은 미등록으로 취급된다(ResolveProfile 폴백 대상)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TObjectPtr<UMaterialInterface> OverlayMaterial = nullptr;

	// 플래시 길이(실초). 실시간 기준이라 히트스톱 중에도 화면상 길이가 같다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float Duration = 0.18f;

	// 발광 배수. 최종 강도 = 이 값 x IntensityByDamage 평가값
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float Intensity = 2.0f;

	// 플래시 색
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FLinearColor Color = FLinearColor::White;

	// 감쇠 지수. 1 = 선형, 1 초과 = 초반에 확 죽는 임팩트형
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float FalloffExponent = 1.0f;

	// 예약 파라미터(림 라이트). 지금 머티리얼은 읽지 않지만 값은 push 된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float FresnelStrength = 0.0f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float FresnelExponent = 2.0f;

	// 국소 플래시 반경(cm). 0 = 메시 전체, 0 보다 크면 맞은 점 둘레만 밝힌다. 예고 발광은 이 값과 상관없이 메시 전체다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0"))
	float LocalRadiusCm = 0.0f;

	// 국소 플래시의 밝은 심 길이(실초). Duration 안에서 심이 먼저 꺼지고 나머지는 옅은 잔광이다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel", meta=(ClampMin="0"))
	float CoreSeconds = 0.03f;

	// 데미지량 -> 강도 배수. 키 0개면 1.0(= 배수 없음)
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FRuntimeFloatCurve IntensityByDamage;

	// 오버레이 드로우 패스의 원거리 컷(cm). 0 = 무제한. 오버레이는 액터 생애 동안 유지된다
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	float MaxDrawDistance = 0.0f;
};

/**
 * 피격 플래시 데이터. 큐 태그 -> 표현 프로필. 맞은 캐릭터의 코스메틱이라 피격자가 소유한다.
 * 키가 태그인 이유: 실행 태그(OriginalTag)는 클라에서 신뢰할 수 있는 유일한 분기 키다.
 * 폴백 순서: 정확 일치 -> 부모 태그 순회 -> DefaultProfile.
 */
UCLASS(BlueprintType)
class VB_API UVBHitFlashConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 태그 미등록이나 미할당 때 쓰는 프로필
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	FVBHitFlashProfile DefaultProfile;

	// 큐 태그별 오버라이드. 미등록 = DefaultProfile 폴백
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|GameFeel")
	TMap<FGameplayTag, FVBHitFlashProfile> TagOverrides;

	// 큐 태그로 표현 프로필을 해석한다. 반환 참조는 이 자산이 소유한다
	const FVBHitFlashProfile& ResolveProfile(const FGameplayTag& CueTag) const;
};
