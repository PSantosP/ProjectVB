// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "VBAIController.generated.h"

struct FVBDeathFacts;
class UAISenseConfig_Damage;
class UAISenseConfig_Sight;
class UVBEnemyConfig;

/**
 * VB 적 AI 컨트롤러
 * Sight Perception으로 감지 -> Blackboard 업데이트 -> BT 실행
 */
UCLASS()
class VB_API AVBAIController : public AAIController
{
	GENERATED_BODY()

public:
	AVBAIController();

	virtual void UpdateControlRotation(float DeltaTime, bool bUpdatePawn = true) override;

protected:
	virtual void OnPossess(APawn* InPawn) override;

	// 행동 트리 시작. 조우 문(UVBEncounterGateComponent)이 열릴 때도 여기로 온다.
	void StartBehavior();

	// 조우 보류(시네마틱 장면 동안)가 바뀌면 행동 트리를 멈추고 잇는다.
	void HandleEncounterHeldChanged(bool bHeld);

	// 빙의가 풀리거나 컨트롤러가 사라질 때 조우 문 구독을 푼다.
	virtual void OnUnPossess() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 조우 문 구독을 푼다. 다시 빙의할 때 처리기가 겹치지 않게.
	void UnbindEncounterGate();
	TWeakObjectPtr<class UVBEncounterGateComponent> BoundEncounterGate;
	FDelegateHandle EncounterHeldHandle;
	FDelegateHandle EncounterWokenHandle;

	// 죽음 알림(Message.Death). 지각은 죽음을 모르므로 죽은 대상을 여기서 놓는다.
	void HandleActorDied(FGameplayTag Channel, const FVBDeathFacts& Facts);
	// 블랙보드 대상이 DeadActor 일 때만 비운다 - 다른 대상을 쫓던 AI 는 건드리지 않는다.
	void ClearTargetIfItIs(const AActor* DeadActor);
	FVBMessageListenerHandle DeathListener;

protected:
	virtual void BeginPlay() override;
	
	// Behaviour Tree (BP에서 설정)
	UPROPERTY(EditDefaultsOnly, Category="VB|AI")
	TObjectPtr<UBehaviorTree> DefaultBehaviorTree;
	
	// BlackboardKey 이름 (BB에셋과 일치해야함)
	UPROPERTY(EditDefaultsOnly, Category="VB|AI")
	FName TargetActorKeyName = "TargetActor";

	// 접근 정지 거리 키. MoveTo 가 시작 때 한 번 읽으므로 서비스 주기 외에 계획이 바뀐 순간에도 쓴다.
	UPROPERTY(EditDefaultsOnly, Category="VB|AI")
	FName AttackRangeKeyName = "AttackRange";

public:
	// 접근 정지 거리를 블랙보드에 바로 쓴다. 전투 컴포넌트가 계획을 바꿀 때 부른다.
	void PushAttackStartDistance(float Distance);

	// 시야 감각 설정(생성자에서 생성, GC 용 UPROPERTY).
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Sight> SightConfig;

	// 피격 감각 - 뒤에서 맞아도 공격자를 안다. 같은 처리기로 받아 BB 타겟의 쓰는 쪽이 하나다.
	UPROPERTY()
	TObjectPtr<UAISenseConfig_Damage> DamageConfig;

	// Perception 감지 콜백
	UFUNCTION()
	void OnTargetPerceptionInfoUpdated(const FActorPerceptionUpdateInfo& UpdateInfo);
	
private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBCombatAuditTestAccess;
	friend struct FVBPerceptionMemoryTestAccess;
#endif

	// 시야에서 잃은 대상과 그것을 잊을 타이머(SightMaxAge). 다시 보이면 거둔다.
	TWeakObjectPtr<AActor> LostTarget;
	FTimerHandle LostTargetTimerHandle;
	// 기억 시간이 지났다 - 아직 그 대상을 노리고 지금도 안 보이면 블랙보드에서 비운다.
	void ForgetLostTarget();
	void CancelLostTargetMemory();
	bool IsSensingNow(AActor& Actor);
	// 피격 감각은 소속 필터가 없어 Hostile 출처만 받는다.
	bool AcceptsStimulusFrom(const FAIStimulus& Stimulus, const AActor& Source) const;
	bool bReportedAttackRangeWriteFailure = false;

	// 시야 수치를 SightConfig 에 쓰고 ConfigureSense 로 반영한다. 생성자와 OnPossess 가 같이 쓴다.
	void ApplyPerceptionConfig(const UVBEnemyConfig* Config);
};
