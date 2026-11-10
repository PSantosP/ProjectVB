// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AbilitySystem/Abilities/VBFinisherStrikeFeedback.h"
#include "VBGA_MedicalAbilityBase.h"
#include "VBGA_Execute.generated.h"

class UVBFinisherComponent;
class UVBFinisherConfig;
struct FHitResult;

/**
 * 처형 어빌리티. 처형 창이 열린 적에게 즉사 피해 + 명성 감소.
 * 무기에 처형 연출이 있으면 장면을 걸고 끝날 때 피해를 주고, 없으면 즉시 처치한다.
 */
UCLASS(Abstract)
class VB_API UVBGA_Execute : public UVBGA_MedicalAbilityBase
{
	GENERATED_BODY()

public:
	UVBGA_Execute();

protected:
	virtual void ExecuteAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, const FGameplayEventData* TriggerEventData) override;

	virtual void EndAbility(
		const FGameplayAbilitySpecHandle Handle, const FGameplayAbilityActorInfo* ActorInfo,
		const FGameplayAbilityActivationInfo ActivationInfo, bool bReplicateEndAbility, bool bWasCancelled) override;

	// 모양 표가 큐를 정하지 않은 모양에 쓸 타격 큐. 비면 피 연출을 안 한다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Finisher|Gore")
	FGameplayTag GoreCueTag;

	// 창이 끝날 때까지 안 닿았어도 날이 대상 캐릭터에서 이 거리(cm) 안이면 가장 가까운 점에 한 번 낸다. 0 이면 끈다.
	UPROPERTY(EditDefaultsOnly, Category = "VB|Finisher|Gore", meta = (ClampMin = "0", Units = "cm"))
	float GoreNearMissReach = 25.0f;

private:
	// 이번 활성화의 갈래. 활성화마다 한 번 고른다. Instant 는 장면 없이 부모의 즉시 처치다.
	enum class EFinisherPath : uint8
	{
		Instant,
		Contextual,
		Interaction,
		Refused,
	};

	// 이번 활성화의 갈래를 고른다. Contextual 이면 OutConfig 에 처형 설정을 담는다.
	EFinisherPath PickFinisherPath(const UVBFinisherComponent& Finisher, AActor* Avatar, AActor* Target,
		const UVBFinisherConfig*& OutConfig) const;

	// 상호작용 장면을 건다. 실패하면 false 이고 그 활성화는 피해 없이 취소한다.
	bool StartInteractionScene(UVBFinisherComponent& Finisher, AActor* Target);

	// 처형 설정 장면을 건다. 실패하면 false 이고 즉시 처치로 떨어진다.
	bool StartContextualScene(UVBFinisherComponent& Finisher, const UVBFinisherConfig* Config, AActor* Target);

	// 처형 컴포넌트의 끝 알림과 착지 알림을 받는다. 뗄 때 같은 컴포넌트를 쓰도록 기억한다.
	void SubscribeFinisher(UVBFinisherComponent& Finisher);
	void UnsubscribeFinisher();

	// 처형 장면이 끝났다. 갈래마다 마무리하고 어빌리티를 끝낸다.
	UFUNCTION()
	void HandleFinisherEnded();

	// 상호작용 장면의 끝. 피해는 착지에서 이미 줬다.
	void FinishInteractionScene();

	// 처형 설정 장면의 끝. 피해를 주고 캡슐을 맞춘 뒤 살아남은 대상을 일으킨다.
	void FinishContextualScene();

	// 메시가 그려진 자리로 캡슐을 데려온다. 사망, 물리, 기상이 맡는 대상은 건드리지 않는다.
	void RebaseVictimCapsule(AActor* Target) const;

	// 버틴 대상에게 기상 이벤트를 보낸다. 피해 뒤에 불러야 죽을 대상을 살아 있다고 읽지 않는다.
	void SendFinisherSurvived(AActor* Target) const;

	// 피해자 notify 시점에 권위측 피해를 한 번 확정한다.
	UFUNCTION()
	void HandleInteractionVictimLanded(FVector LaunchVelocity);

	// 진행 중인 내 공격을 끊는다. 장면 시작 전에 부른다.
	void CancelOwnAttacks();

	// 처형 몽타주의 판정 창 이벤트를 구독한다. 연출 본문은 StrikeFeedback 이 맡는다.
	void BeginFinisherGore();
	UFUNCTION()
	void OnFinisherStrikeBegin(FGameplayEventData Payload);
	UFUNCTION()
	void OnFinisherStrikeEnd(FGameplayEventData Payload);
	void OnFinisherGoreHits(const TArray<FHitResult>& Hits, int32 Slot);
	FVBFinisherStrikeContext MakeStrikeContext() const;

	// 처형 장면 하나의 상태. 장면이 끝나면 EndAbility 가 통째로 비운다.
	struct FFinisherScene
	{
		// Instant 면 장면이 없다.
		EFinisherPath Path = EFinisherPath::Instant;
		// 이번 처형 피해를 이미 줬는가. 두 번 주면 명성도 두 번 깎인다.
		bool bDamageApplied = false;
		// 장면을 걸 때 잡은 대상. 끝날 때 다시 찾지 않고 이 액터에 피해를 준다.
		TWeakObjectPtr<AActor> Target;
		// 컴포넌트의 ActiveConfig 는 장면 끝에 비워지므로 시작 때 잡아 둔다.
		TWeakObjectPtr<const UVBFinisherConfig> Config;
		TWeakObjectPtr<UVBFinisherComponent> Finisher;
	};

	FFinisherScene Scene;
	FVBFinisherStrikeFeedback StrikeFeedback;
};
