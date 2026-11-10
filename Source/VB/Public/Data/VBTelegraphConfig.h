// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "VBTelegraphConfig.generated.h"

class UMaterialInterface;
class USoundAttenuation;
class USoundBase;

/** 답(Attack.Answer.*) 하나의 연출 묶음 - 색, 메시 발광 세기와 감쇠, 답 소리. */
USTRUCT(BlueprintType)
struct FVBTelegraphAnswerCue
{
	GENERATED_BODY()

	// 이 답의 색. 메시 발광과 HUD 가장자리 화살표가 같은 값을 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	FLinearColor Color = FLinearColor::White;

	// 메시 발광 세기(오버레이 FlashIntensity). 피격 플래시와 구분되게 둔다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph", meta=(ClampMin="0.0"))
	float GlowIntensity = 1.5f;

	// 발광 감쇠 지수. 1 = 선형, 1 미만 = 예고 내내 밝다가 끝에 꺼지는 유지형.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph", meta=(ClampMin="0.01"))
	float GlowFalloffExponent = 0.35f;

	// 답 소리(3D, 적에 부착). 공통 예고 소리 위에 겹친다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<USoundBase> AnswerSound = nullptr;
};

/**
 * 적 공격 예고 연출 데이터. 답 태그별 연출 + 공통 연출(발광 머티리얼, 기합, 휘두름, 감쇠, 화면 밖 경고음).
 * 답의 색은 게임 전체 약속이라 공격 자산이 아니라 여기 하나에 둔다. 저작 지점 = GCN_Telegraph 클래스 기본값.
 */
UCLASS(BlueprintType)
class VB_API UVBTelegraphConfig : public UPrimaryDataAsset
{
	GENERATED_BODY()

public:
	// 답 태그별 연출. 미등록 답은 공통 연출만 나가고 큐가 Warning 을 찍는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph", meta=(Categories="Attack.Answer"))
	TMap<FGameplayTag, FVBTelegraphAnswerCue> AnswerCues;

	// 메시 발광 오버레이 머티리얼. 피격 플래시와 같은 MI 를 꽂는다 - 다르면 예고마다 오버레이를 다시 거는 비용을 낸다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<UMaterialInterface> GlowOverlayMaterial = nullptr;

	// 예고 공통 소리(3D, 적에 부착). 기합 = 누가, 휘두름 = 언제. 둘 다 선택이다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<USoundBase> TelegraphVoice = nullptr;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<USoundBase> TelegraphWhoosh = nullptr;

	// 위 3D 소리들의 감쇠. 미할당이면 엔진 기본(감쇠 없음 = 어디서나 같은 크기)이라 방향을 못 읽는다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<USoundAttenuation> Attenuation = nullptr;

	// 예고에 들어간 적이 화면 밖일 때 한 번 재생하는 2D 경고음.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Telegraph")
	TObjectPtr<USoundBase> OffScreenWarningSound = nullptr;

	// 답 태그로 연출 행 조회. 없으면 nullptr.
	const FVBTelegraphAnswerCue* FindAnswerCue(const FGameplayTag& Answer) const;
};
