// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/NetSerialization.h"
#include "AbilitySystem/VBHitImpact.h"
#include "CharacterCommon/VBRagdollBody.h"
#include "VBEnemyRagdollComponent.generated.h"

class AVBEnemyBase;
class UVBEnemyConfig;
class UVBHitPhysicsComponent;
class UVBRagdollComponent;
struct FHitResult;

// 살아날 쓰러짐의 복제 단계. None 이면 애니메이션 몸이다.
UENUM()
enum class EVBRecoverableRagdollPhase : uint8
{
	None,
	Ragdoll,
	GettingUp
};

// multicast 때 relevant 하지 않던 클라이언트를 위한 지속 단계.
USTRUCT()
struct FVBEnemyRecoverableRagdollState
{
	GENERATED_BODY()
	UPROPERTY() EVBRecoverableRagdollPhase Phase = EVBRecoverableRagdollPhase::None;
	UPROPERTY() FVector_NetQuantize10 LaunchVelocity = FVector::ZeroVector;
	UPROPERTY() double ServerStartTime = 0.0;
};

// 시체 물리에 들어갈 때의 첫 속도. 죽음 연출이 채우고, 안정화가 운동량을 지울지 이것으로 정한다.
struct FVBCorpseLaunch
{
	// 던져진 래그돌이라 운동량을 지우지 않는다.
	bool bKeepMomentum = false;
	// 래그돌을 켤 때 전 바디에 설정할 월드 속도(cm/s). 0 이면 발사하지 않는다.
	FVector Velocity = FVector::ZeroVector;
	// 늦게 받은 기계라 발사를 재연하지 않는다.
	bool bSuppressLaunch = false;
};

// 적 몸의 물리 세션 하나를 맡는다: 살아날 쓰러짐, 시체 물리 진입, 캡슐 따라가기, 쓰러진 몸 충격.
// 뼈 물리 제어는 UVBRagdollComponent, 상태 없는 몸 계산은 VBRagdollBody 가 한다.
// 죽었는지는 체력 컴포넌트가, 언제 시체가 되는지는 UVBEnemyDeathPresentationComponent 가 정한다.
UCLASS(ClassGroup=(VB))
class VB_API UVBEnemyRagdollComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBEnemyRagdollComponent();

	// AVBEnemyBase 가 만든다. 없으면 적이 아니다.
	static UVBEnemyRagdollComponent* FindFor(const AActor* Actor);

	// 살아남은 피해자를 쓰러뜨린다(서버). 멎으면 기상 능력을 부른다. 시작했으면 true.
	bool StartRecoverable(const FVector& LaunchVelocity);
	// 기상을 끝내고 메시를 애니메이션으로 되돌린다(서버).
	void FinishGetUp();
	// 살아날 쓰러짐이 진행 중이면 true.
	bool IsRecoverableActive() const { return Recoverable.bActive; }

	// 물리로 누워 있어 타격의 이동을 바디가 받으면 true. 기상 중은 아니다.
	bool IsDownedInPhysics() const;
	// 쓰러진 몸에 속도 충격을 준다(서버). 준 속도의 크기를 돌려준다.
	float ReceiveDownedImpact(const FHitResult* Hit, const FVector& PushDir, EVBHitImpact Impact, float KnockbackScale);

	// 사망 동작과 물리로 전환하기 전에 캡슐 충돌을 끄고 이동을 멈춘다.
	void StopBodyForDeath();
	// 시체 물리로 전환한다. 처음 한 번만 true. 쓰러진 채면 누운 몸을 그대로 시체로 넘긴다.
	bool EnterCorpse(const FVBCorpseLaunch& Launch, double ElapsedSinceDeath);
	// 시체 물리에 들어갔으면 true.
	bool HasEnteredCorpse() const { return bEnteredCorpse; }

	// 몸 BeginPlay 끝(ASC 초기화 뒤)에 부른다. 먼저 와 있던 복제 단계를 적용한다.
	void MarkBodyReady();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void OnRegister() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// 살아날 쓰러짐을 모든 컴퓨터에서 시작한다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartRecoverable(const FVector& LaunchVelocity);
	// 기상을 모든 컴퓨터에서 시작한다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartGetUp();
	// 기상을 모든 컴퓨터에서 끝낸다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastFinishGetUp();
	// 쓰러진 몸에 속도 충격(모든 머신 - 래그돌은 머신마다 따로 돈다). 판정과 기상은 서버가 정하므로 Unreliable.
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastDownedImpact(FVector_NetQuantize Point, FName Bone, FVector_NetQuantize Velocity);

	// multicast 를 놓친 컴퓨터가 쓰러짐 단계를 받았다.
	UFUNCTION()
	void OnRep_RecoverableState();

	// 살아날 쓰러짐을 이 머신의 메시에 건다. 돌려줄 충돌 설정을 먼저 저장한다.
	void ApplyRecoverable(const FVector& LaunchVelocity);
	// 물리 자세를 애니메이션으로 전환하고 기상 능력을 시작한다.
	void ApplyGetUp();
	// 저장한 충돌 설정과 태그를 되돌리고 쓰러짐을 끝낸다.
	void ResetRecoverable();
	// 살아날 쓰러짐을 죽음 쪽으로 내린다. 누운 몸을 그대로 시체로 넘길 수 있으면 true.
	bool ReleaseRecoverableForDeath();
	// 이미 물리로 누운 몸을 시체로 넘긴다. 멎음과 상한은 Now 부터 다시 잰다.
	void HandDownedBodyToDeath(double Now);
	// 메시 충돌을 래그돌로 바꾸고 시뮬레이션을 켠다.
	void SwitchMeshToRagdoll();
	// 시뮬레이션을 켠 뒤 첫 속도를 심거나 지운다.
	void ApplyCorpseEntryVelocity();
	// 시체가 운동량을 남기면 true. 지우는 자리 셋이 이 판정 하나를 쓴다.
	bool ShouldPreserveCorpseMomentum() const;
	// 시체가 물려받은 속도를 다음 틱과 0.3초 뒤에 재고 지운다 - 켠 프레임에는 0 으로 읽힌다(Chaos 가 다음 단계에서 심는다).
	void SettleCorpseVelocity();

	// 캡슐 따라가기를 시작한다. 시뮬레이션을 켜고 첫 속도를 심은 뒤에 부른다.
	void BeginFollow(const FVector& EntryVelocity);
	// 캡슐 따라가기 한 틱.
	void TickFollow(float DeltaTime);
	// 따라가기를 끝낸다. 멎었거나 상한에 걸렸을 때.
	void EndFollow(const TCHAR* Reason);
	// 뼈 아래 바닥을 찾아 캡슐이 갈 자리를 돌려주고 바닥 유무를 남긴다.
	FVector ResolveFollowTarget(const FVector& BoneLocation);
	// 살아날 쓰러짐이 멎었으면 기상을 시작하고 true(서버).
	bool TryStartGetUp(const UVBEnemyConfig& Config, double Now);
	// 바디가 멎었거나 상한을 넘었으면 따라가기를 끝낸다.
	void CheckFollowSettled(const UVBEnemyConfig& Config, double Now);

	const UVBEnemyConfig* GetConfig() const;
	bool IsBodyDead() const;

	UPROPERTY(ReplicatedUsing=OnRep_RecoverableState)
	FVBEnemyRecoverableRagdollState ReplicatedRecoverable;

	// 캡슐 따라가기 한 번. 이것이 켜진 동안에만 컴포넌트 틱이 돈다.
	struct FVBRagdollFollow
	{
		bool bActive = false;
		FName Bone;
		double StartTime = 0.0;
		FVector StartLocation = FVector::ZeroVector;
		// 문턱 아래로 연속 몇 프레임 있었나. 한 프레임만 보면 솔버가 튀는 프레임에 속는다.
		int32 SettledFrames = 0;
		bool bHasFloor = false;
		double NextGetUpCheckTime = 0.0;
	};
	FVBRagdollFollow Follow;

	// 살아날 쓰러짐 한 번. 기상이 끝나거나 죽음으로 넘어가면 비운다.
	struct FVBRecoverableDown
	{
		bool bActive = false;
		// 서버: 이 쓰러짐이 Downed 를 붙였나. 붙인 쪽만 뗀다 - 넘어짐 GA 의 Downed 와 개수가 섞이지 않게.
		bool bDownedTagGranted = false;
		FVBRagdollCollisionState CapsuleCollision;
		FVBRagdollCollisionState MeshCollision;
	};
	FVBRecoverableDown Recoverable;

	// 시체는 한 번만 만든다. 몽타주 종료와 안전망 타이머가 둘 다 도착할 수 있다.
	bool bEnteredCorpse = false;
	FVBCorpseLaunch CorpseLaunch;

	// 몸 BeginPlay 전에 온 복제 단계는 적용을 미룬다.
	bool bBodyReady = false;

	UPROPERTY(Transient)
	TObjectPtr<AVBEnemyBase> Body;
	UPROPERTY(Transient)
	TObjectPtr<UVBRagdollComponent> PhysicsControl;
	UPROPERTY(Transient)
	TObjectPtr<UVBHitPhysicsComponent> HitPhysics;
};
