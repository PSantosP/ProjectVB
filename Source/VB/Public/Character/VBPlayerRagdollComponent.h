// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "Animation/AnimMontage.h"
#include "Engine/EngineTypes.h"
#include "Engine/NetSerialization.h"
#include "CharacterCommon/VBRagdollBody.h"
#include "VBPlayerRagdollComponent.generated.h"

class ACharacter;
struct FHitResult;
class UAbilitySystemComponent;
class UCapsuleComponent;
class UCharacterMovementComponent;
class USkeletalMeshComponent;
class UVBRagdollComponent;
class UVBRagdollConfig;
class UWorld;
struct FCollisionQueryParams;

UENUM(BlueprintType)
enum class EVBPlayerRagdollPhase : uint8
{
	None,
	Ragdoll,
	GettingUp
};

USTRUCT()
struct FVBPlayerRagdollState
{
	GENERATED_BODY()
	UPROPERTY() EVBPlayerRagdollPhase Phase = EVBPlayerRagdollPhase::None;
	UPROPERTY() FVector_NetQuantize10 EntryVelocity = FVector::ZeroVector;
};

/** CMC 플레이어의 래그돌 수명 주기. physics, 구르기 동작은 공용 VBRagdollComponent 에 있다. */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBPlayerRagdollComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	// 일어나기 바닥 쓸기의 기하(골반 위 반경에서 시작해 아래로 2 x 반높이). 무엇 안에서 시작했으면 거짓.
	static bool SweepGetUpFloor(const UWorld& World, const FVector& Pelvis, float Radius, float HalfHeight,
		const FCollisionQueryParams& Params, FHitResult& OutHit);
	UVBPlayerRagdollComponent();
	// AVBCharacter 가 만든다. 없으면 플레이어 래그돌이 없는 액터다.
	static UVBPlayerRagdollComponent* FindFor(const AActor* Actor);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ragdoll")
	TObjectPtr<UVBRagdollConfig> Config;
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category="VB|Ragdoll")
	FMontageBlendSettings EntryMontageBlendOut;

	// 원본 GASP 와 같다 - 래그돌 입력으로 들어가고, 바닥 위에서 점프가 회복을 요청한다.
	UFUNCTION(BlueprintCallable, Category="VB|Ragdoll")
	void RequestRagdoll();
	UFUNCTION(BlueprintCallable, Category="VB|Ragdoll")
	void RequestGetUp();
	void SetRollInput(const FVector& WorldInput);

	UFUNCTION(BlueprintPure, Category="VB|Ragdoll")
	bool IsRagdollActive() const { return LocalPhase != EVBPlayerRagdollPhase::None; }
	UFUNCTION(BlueprintPure, Category="VB|Ragdoll")
	bool IsRagdolling() const { return LocalPhase == EVBPlayerRagdollPhase::Ragdoll; }
	UFUNCTION(BlueprintPure, Category="VB|Ragdoll")
	EVBPlayerRagdollPhase GetPhase() const { return LocalPhase; }

	// 회복 GA 가 montage 재생 전에 부른다. phase 복제 뒤에 불러도 안전하다.
	bool PrepareForGetUp();
	void FinishRecovery();
	// 죽음, 복원이 mesh, collision, movement 를 가져가기 전에 부른다.
	void CancelRagdoll();
	// PlayerState/ASC 는 래그돌 phase 복제 뒤에 생길 수 있어 다시 묻는다.
	void RefreshAbilityState();
	static FGameplayTag GetRecoveryEventTag();

protected:
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	UFUNCTION(Server, Reliable) void ServerRequestRagdoll();
	UFUNCTION(Server, Reliable) void ServerRequestGetUp();
	UFUNCTION(Server, Unreliable) void ServerSetRollInput(FVector_NetQuantize10 WorldInput);
	UFUNCTION() void OnRep_State();
	UPROPERTY(ReplicatedUsing=OnRep_State) FVBPlayerRagdollState State;
	UPROPERTY(Replicated) FVector_NetQuantize10 RollInput = FVector::ZeroVector;

	bool StartAuthority();
	bool BeginRagdoll(const FVector& EntryVelocity);
	// 래그돌이 바꿀 충돌, 이동, 회전 값을 Restore 가 쓸 수 있게 남긴다.
	void SaveBodyState(const UCapsuleComponent& Capsule, const USkeletalMeshComponent& Mesh, const UCharacterMovementComponent& Movement);
	// 조준과 락온을 끈다(서버와 소유 기계).
	void ClearAimAndLockOn();
	// 지금 몽타주를 점유한 능력을 끝낸다(서버).
	void CancelAnimatingAbility();
	// 점프, 컨트롤러 회전, 이동 틱, 이동 복제를 멈춘다.
	void StopBodyMovement(UCharacterMovementComponent& Movement);
	// 캡슐을 월드 전용으로 바꾸고 메시를 골반 아래부터 물리로 전환한다.
	void SwitchBodyToPhysics(UCapsuleComponent& Capsule, USkeletalMeshComponent& Mesh, const FVector& EntryVelocity);
	void StartGetUpAuthority();
	void Restore(bool bCompletedRecovery = false);
	// 골반 아래 걸을 수 있는 바닥(일어나기, 누운 동안 캡슐 따라가기가 같이 쓴다). bOutStartedInside = 쓸기가 무엇 안에서 시작했다(낮은 천장 등).
	bool ProbeFloor(FVector& OutLocation, FVector& OutNormal, bool* bOutStartedInside = nullptr) const;
	// 래그돌 상태 태그(State.Combat.Ragdoll)를 붙이거나 뗀다.
	void SetRagdollStateTag(bool bOn);
	UAbilitySystemComponent* GetASC() const;

	UPROPERTY(Transient) TObjectPtr<ACharacter> Character;
	UPROPERTY(Transient) TObjectPtr<UVBRagdollComponent> Physics;
	// 래그돌 태그를 붙인 ASC. 아바타가 바뀌면 이전 ASC 에서 뗀다.
	UPROPERTY(Transient) TObjectPtr<UAbilitySystemComponent> TaggedASC;
	EVBPlayerRagdollPhase LocalPhase = EVBPlayerRagdollPhase::None;
	FVector LocalRollInput = FVector::ZeroVector;
	double LastRollInputSendTime = -1.0;
	double LastServerRollInputTime = -1.0;
	bool bHasFloor = false;
	bool bSavedState = false;
	bool bMovementTickWasEnabled = false;
	bool bReplicateMovementWasEnabled = false;
	bool bUseControllerYawWasEnabled = false;
	uint8 SavedMovementMode = 0;
	uint8 SavedCustomMovementMode = 0;
	FTransform MeshRelativeTransform;
	FVBRagdollCollisionState CapsuleCollision;
	FVBRagdollCollisionState MeshCollision;
};
