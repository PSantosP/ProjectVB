// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "Engine/TimerHandle.h"
#include "CharacterCommon/VBDeathFacts.h"
#include "Enemy/VBEnemyRagdollComponent.h" // FVBCorpseLaunch - 값 멤버
#include "VBEnemyDeathPresentationComponent.generated.h"

class AVBEnemyBase;
class APawn;
class UAnimMontage;
class UVBDeathDissolveComponent;
class UVBHitFlashComponent;
class UVBHitPhysicsComponent;
struct FHitResult;

// RPC 를 놓친 클라이언트도 현재 사망 단계를 복원하기 위한 지속 상태.
USTRUCT()
struct FVBEnemyDeathState
{
	GENERATED_BODY()
	UPROPERTY() bool bDead = false;
	UPROPERTY() bool bKeepMomentum = false;
	UPROPERTY() bool bAirborne = false;
	UPROPERTY() FVBDeathMotion AirMotion;
	UPROPERTY() bool bRagdoll = false;
	UPROPERTY() bool bHasDeathMontage = false;
	UPROPERTY() FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
	UPROPERTY() double ServerDeathTime = 0.0;
	UPROPERTY() float RagdollDelay = 0.0f;
	UPROPERTY() TObjectPtr<UAnimMontage> DeathMontage = nullptr;
};

// 적 죽음의 복제 스냅샷 하나와 그 재생을 맡는다: 서버에서 스냅샷을 만들고, 기계마다 한 번 재생하고(공중 사망, 몽타주, 곧바로 시체), 시체 정리를 예약한다.
// 죽음 판정과 순서는 체력 컴포넌트, 시체 물리는 UVBEnemyRagdollComponent, 디졸브 실행은 UVBDeathDissolveComponent 가 한다.
UCLASS(ClassGroup=(VB))
class VB_API UVBEnemyDeathPresentationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEnemyDeathPresentationComponent();

	// AVBEnemyBase 가 만든다. 없으면 적이 아니다.
	static UVBEnemyDeathPresentationComponent* FindFor(const AActor* Actor);

	// 죽는 순간의 공중 상태와 반응 몽타주를 스냅샷에 남긴다(서버, 사망 태그 전).
	void CaptureAirDeath(const FVBDeathFacts& Facts);
	// 스냅샷을 완성해 모든 컴퓨터에 보낸다(서버, 사망 태그 뒤).
	void Present(const FVBDeathFacts& Facts);

	// 이번 죽음은 던져진 것이라 운동량을 지우지 않는다. 사망을 일으키기 전에 부른다.
	void KeepMomentum(const FVector& LaunchVelocity);
	// 처형 피해를 버틴 대상에게 다음 사망용 입력을 남기지 않는다.
	void ClearPendingMomentum();

	// 죽음 스냅샷을 받았으면 true. 원격 기계가 State.Dead 복제 전에 시체를 겨누지 않게 몸의 IsDead 가 묻는다.
	bool IsDeathReplicated() const { return Snapshot.bDead; }

	// 몸 BeginPlay 끝(ASC 초기화 뒤)에 부른다. 먼저 와 있던 죽음을 재생한다.
	void MarkBodyReady();

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// 실시간 재생 전달. 서버의 죽음 순서는 체력 컴포넌트의 것이다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastPresent(const FVBEnemyDeathState& State);

private:
	// multicast 를 놓친 컴퓨터가 사망 상태를 받았다.
	UFUNCTION()
	void OnRep_Snapshot();
	// 복제로만 사망을 받은 컴퓨터의 적용 재시도다.
	void ReplayState();
	// 스냅샷을 이 컴퓨터에 적용한다. 같은 스냅샷을 두 번 시작하지 않는다.
	void ApplyState(bool bFromLiveEvent);
	// 복제할 사망 몽타주와 래그돌까지의 시간을 정한다.
	void ChooseMontage(bool bWasDowned);

	// 사망 연출을 시작한다. 공중 사망, 몽타주, 곧바로 시체 중 하나로 간다.
	void PlayCosmetic(float ElapsedSinceDeath);
	// 공중 사망 연출을 시작한다. 조건이 안 맞으면 false.
	bool BeginAirDeath(float ElapsedSinceDeath);
	// 공중 반응 몽타주가 끝났다. 이미 땅이면 시체로 전환한다.
	void OnAirDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);
	// 공중에서 죽은 캐릭터가 땅에 닿았다.
	UFUNCTION()
	void OnAirDeathLanded(const FHitResult& Hit);
	// 공중 사망의 착지 대기를 거둔다.
	void EndAirDeath();
	// 사망 몽타주가 끝났다 - 끊겼든 끝까지 재생됐든 시체로 전환한다.
	void OnDeathMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 시체 물리를 요청하고, 처음이면 정리를 예약한다.
	void EnterCorpse();
	// 시체 정리(디졸브, 파괴)를 예약한다. 정리 지연은 죽은 순간부터 잰다.
	void ScheduleCleanup(double ElapsedSinceDeath);
	void DestroyBody();

	// 몸이 C++ 에서 만드는 필수 형제(몸, 래그돌, 피격 물리)가 다 있으면 true. 없으면 BeginPlay 가 Error 를 남기고 죽음은 시작하지 않는다.
	bool IsAssembled() const;

	UPROPERTY(ReplicatedUsing=OnRep_Snapshot)
	FVBEnemyDeathState Snapshot;

	// 기계마다 한 번의 재생.
	struct FVBDeathPlayback
	{
		// 몸 BeginPlay 전에 온 죽음은 재생을 미룬다.
		bool bBodyReady = false;
		bool bPendingLive = false;
		bool bStarted = false;
		// 사망 연출 시작 시각 - 정리 지연을 죽은 순간부터 잰다.
		double StartTime = 0.0;
		FTimerHandle ReplayTimer;
		FTimerHandle CorpseTimer;
	};
	FVBDeathPlayback Playback;

	// 공중 사망 한 번.
	struct FVBAirDeath
	{
		bool bReactionFinished = false;
		bool bImpactPending = false;
		// 치명타 순간의 공격 Pawn 만 착지까지 보관한다.
		TWeakObjectPtr<APawn> Instigator;
	};
	FVBAirDeath AirDeath;

	// 죽기 전에는 처형과 공중 사망이 남긴 입력, 재생이 시작된 뒤에는 이 기계의 시체 첫 속도다.
	FVBCorpseLaunch Launch;

	UPROPERTY(Transient)
	TObjectPtr<AVBEnemyBase> Body;
	UPROPERTY(Transient)
	TObjectPtr<UVBEnemyRagdollComponent> Ragdoll;
	UPROPERTY(Transient)
	TObjectPtr<UVBHitFlashComponent> HitFlash;
	UPROPERTY(Transient)
	TObjectPtr<UVBHitPhysicsComponent> HitPhysics;
	UPROPERTY(Transient)
	TObjectPtr<UVBDeathDissolveComponent> Dissolve;
};
