// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "VBFinisherWindowConfig.generated.h"

class UAnimMontage;

// 처형 창을 여는 사건. 적마다 고른다.
UENUM(BlueprintType)
enum class EVBFinisherWindowOpenOn : uint8
{
	// 기절 게이지가 다 찬 순간(Event.Combat.PoiseBroken). 이때 경직 GA 는 그 사건을 받지 않는다.
	PoiseBreak,
	// 체력이 HealthThreshold 아래로 떨어진 타격(Event.Combat.FinisherWindow).
	HealthThreshold
};

/**
 * 처형 창의 튜닝값. 창을 여는 쪽(피해자)이 드는 자산이고 UVBGA_FinisherWindow 가 참조한다.
 * 공격자는 이 자산을 읽지 않는다 - 피해자의 창 태그만 본다.
 */
UCLASS(BlueprintType)
class VB_API UVBFinisherWindowConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	// 창을 여는 사건. 체력 문턱은 HealthThreshold 일 때만 쓴다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	EVBFinisherWindowOpenOn OpenOn = EVBFinisherWindowOpenOn::PoiseBreak;

	// OpenOn 이 HealthThreshold 일 때, 이 비율 이하로 떨어진 타격에서 창이 열린다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher",
		meta=(ClampMin="0.0", ClampMax="1.0", EditCondition="OpenOn == EVBFinisherWindowOpenOn::HealthThreshold"))
	float HealthThreshold = 0.30f;

	// 창이 열려 있는 시간(초). 지나면 적이 풀려나 다시 싸운다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.1"))
	float WindowDuration = 4.0f;

	// 누운 적이 일어나기를 기다리는 상한(초). 넘으면 창을 포기하고 경고한다 - 새는 누움 태그가 창을 영영 막지 않게.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher", meta=(ClampMin="0.5"))
	float MaxReactionWaitSeconds = 10.0f;

	// 창 동안의 대기 자세. 비면 태그와 차단만 서고 자세는 안 바뀐다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	TObjectPtr<UAnimMontage> WaitMontage = nullptr;

	// 창이 열릴 때 속도와 가속을 지운다. 이동 잠금이 아니다 - 처형 장면의 워프가 피해자를 끌어야 한다.
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Finisher")
	bool bStopMovementOnOpen = true;
};
