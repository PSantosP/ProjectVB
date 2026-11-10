// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "GameplayCueInterface.h"
#include "Character/VBGuardable.h"
#include "VBDamagePipelineTestVictim.generated.h"

// VB.Unit.Damage.Pipeline 전용 몸 - damage hub 의 guard 질문에 답하고 모든 호출, cue, event 를 순서 로그 하나에 적는다.
//   실제 AVBCharacter 는 guard 상태가 능력과 컴포넌트에 있어 어차피 흉내내야 하므로 쓰지 않는다.
UCLASS(NotBlueprintable, NotPlaceable)
class AVBDamagePipelineTestVictim : public ACharacter, public IVBGuardable, public IGameplayCueInterface
{
	GENERATED_BODY()

public:
	AVBDamagePipelineTestVictim();

	// 이 피해자의 패링 설정. 패링 게이지(ParryPoiseDamage)는 끈다 - 그 줄은 VB.Unit.Combat.PostureExecute 가 잰다.
	UPROPERTY(Transient)
	TObjectPtr<UVBParryConfig> ParryConfig;

	// 테스트가 경우마다 정하는 guard 답.
	bool bGuarding = false;
	float GuardElapsed = 0.0f;

	// 테스트가 가진 공용 순서 로그.
	TArray<FString>* Log = nullptr;
	// 마지막 실행 cue 의 parameters - location, normal, instigator 확인용.
	FGameplayCueParameters LastCue;

	// IVBGuardable
	virtual bool IsGuarding() const override { return bGuarding; }
	virtual float GetGuardElapsed() const override { return GuardElapsed; }
	virtual void PlayGuardAccept(EVBHitDirection Side) override;
	virtual const UVBParryConfig* GetParryConfig() const override;
	virtual void OpenRiposteWindow(float Duration, AActor* Attacker) override;
	virtual void ApplyGuardStaminaDelta(float Delta) override;
	// 켜면 공용 규칙 대신 이 몸이 guard 질문에 직접 답한다 - health set 이 guard 주인의 답을 따르는지 본다.
	bool bOwnGuardRule = false;
	virtual FVBGuardResult ResolveIncomingHit(UAbilitySystemComponent& VictimASC, const FGameplayEffectSpec& Spec, float Damage) override;

	// IGameplayCueInterface
	virtual void HandleGameplayCue(UObject* Self, FGameplayTag GameplayCueTag, EGameplayCueEvent::Type EventType,
		const FGameplayCueParameters& Parameters) override;
};
