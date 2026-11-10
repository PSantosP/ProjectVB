// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Templates/Function.h"
#include "CommonInputTypeEnum.h"
#include "Character/VBTargetLockPolicy.h"
#include "AbilitySystem/VBLooseTagHold.h"
#include "Data/VBTargetLockConfig.h"
#include "VBTargetLockComponent.generated.h"

class AVBCharacter;
class APlayerController;
class UVBTargetLockConfig;
class UVBWeaponStateComponent;

UENUM()
enum class EVBLockReason : uint8
{
	Manual, Switch, Guard, Hit, Parry, Reacquire, TargetDied, InvalidTarget, OutOfRange, Occluded, Gameplay, OwnerChanged,
	// 플레이어 해제 중 락 키가 아닌 것 - Manual 과 로그로 가르려고 따로 둔다(판정은 같다, IsPlayerRelease).
	RetreatSprint
};

// 대상과 revision은 한 속성으로 복제한다. 카메라 중단은 전투 대상을 지우지 않는다.
USTRUCT()
struct FVBTargetLockState
{
	GENERATED_BODY()
	UPROPERTY() TObjectPtr<AActor> Target = nullptr;
	UPROPERTY() uint32 Revision = 0;
	UPROPERTY() EVBLockReason Reason = EVBLockReason::Gameplay;
};

// 누구를 잡고 있는지의 서버 정본과 복제, 언제 놓는지, 바꿔 달라는 요청을 맡는다. 고르는 규칙은 VBTargetLockPolicy 에 있다.
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBTargetLockComponent : public UActorComponent
{
	GENERATED_BODY()
public:
	UVBTargetLockComponent();
	// 플레이어 캐릭터만 가진다. 없으면 락온을 하지 않는 액터(적)다.
	static UVBTargetLockComponent* FindFor(const AActor* Actor);
	// 락 키. 잡혀 있으면 놓고, 없으면 화면에서 고른 대상을 요청한다.
	void ToggleLockOn();
	// 그 방향의 다음 대상을 바로 요청한다(AxisValue 는 부호만 본다).
	void SwitchTarget(float AxisValue);
	// 입력 장치별 전환 입력. 패드는 스틱 걸쇠를 거쳐 한 번만 SwitchTarget 을 부른다.
	void HandleSwitchInput(float AxisValue, bool bGamepad);
	void ResetSwitchInput();
	// 락온 중 시점 입력을 화면 오프셋으로 모은다. 회전 합성은 UpdateCameraRotation 이 한다.
	void AddLookInput(float Yaw, float Pitch, bool bGamepad);
	// Look 입력을 control rotation 도로 바꾼다 - APlayerController::AddYawInput/AddPitchInput 과 같은 변환이라 락온 오프셋이 자유 시점과 맞는다.
	static FVector2D LookInputToControlDegrees(const APlayerController* Controller, float Yaw, float Pitch);
	// 지금 카메라가 락온 대상을 따라가도 되면 true.
	bool CanTrackCamera() const;
	// PlayerController의 회전 단계에서 호출하여 카메라 평가/이동 전에 합성한다.
	void UpdateCameraRotation(float DeltaTime);
	/** 현재 락온 대상에 대한 로컬 뒤쪽 구도 요청 */
	uint64 BeginCameraFraming(AActor* Target, const FVBTargetLockFraming& Framing);
	/** 같은 요청이 아직 소유할 때만 구도 해제 */
	void EndCameraFraming(uint64 RequestId);
	bool HasCameraFraming(uint64 RequestId) const { return RequestId != 0 && RequestId == CameraFramingId; }
	bool HasActiveCameraFraming() const { return CameraFramingId != 0; }
	// 이 머신이 확정한 대상(ConfirmedState). 서버 정본 LockState 보다 늦을 수 있다.
	AActor* GetLockedTarget() const;
	// 확정한 상태의 마지막 변경 이유.
	EVBLockReason GetLockReason() const;

	// 락온 중 방향 공격 전환(서버): WorldIntent(이동 입력, 월드 XY)가 분명히 가리키는 다른 적, 없으면 nullptr. 상태를 바꾸지 않는다.
	//   StepMaxDistance 는 이 단계의 허용 거리 - 설정 거리, 락온 획득 거리로도 깎인다.
	AActor* ProposeDirectionalTarget(const FVector2D& WorldIntent, float StepMaxDistance) const;
	// 고른 대상으로 락온을 바로 전환한다(Switch, 중간 해제 없음). ExpectedRevision 이 지금과 다르면(그 사이 다른 전환, 해제) 거절한다.
	bool CommitDirectionalSwitch(AActor* Target, uint32 ExpectedRevision);
	// 서버 정본(LockState)의 revision.
	uint32 GetLockRevision() const { return LockState.Revision; }
	bool IsLockedOn() const;
	// 게임 사정으로 락을 푼다. 서버는 바로, 클라는 요청한다.
	void ClearLockOn();
	// 서버: 공격이 맞은 대상에게 자동으로 락을 건다(이미 잡혀 있거나 해제 억제 중이면 무시).
	void TryAutoLockOnTarget(AActor* Target);
	// 서버에서 확인한 근접 피해를 소유자 화면에만 전달한다. 락 상태/공격 대상은 바꾸지 않는다.
	void NotifyMeleeHitForCamera(AActor* Target);
	// 락온 밖의 시점 입력을 알린다 - 명중 보정을 끊는다.
	void NotifyManualCameraInput(float Yaw, float Pitch);
	// PlayerController 의 회전 단계에서 부른다. 진행 중인 명중 보정을 한 프레임 진행한다.
	void UpdateHitRecenter();
	// 패링 성공(서버): 기존 락을 넘어 공격자에게 락을 걸고 이 프레임에 바라본다(플래그는 UVBParryConfig).
	void FaceAndLockParryTarget(AActor* Target, bool bLock, bool bFace);
	// 로컬 시점에서 고른 대상을 가드 이유로 요청한다.
	UFUNCTION(BlueprintCallable, Category="VB|LockOn")
	void TryLockOnFromLocalView();
	// 액터 정면 근처의 가장 좋은 대상(락온 상태와 무관한 근접 질의).
	AActor* FindSoftLungeTarget() const;
	// FindSoftLungeTarget 과 같은 질의에 부르는 쪽의 조건을 더한다.
	AActor* FindConeTarget(TFunctionRef<bool(const AActor*)> Predicate) const;
	const UVBTargetLockConfig* GetTargetLockConfig() const;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTargetLockChanged, AActor*, TargetActor);
	UPROPERTY(BlueprintAssignable, Category="VB|LockOn") FOnTargetLockChanged OnTargetLocked;
	UPROPERTY(BlueprintAssignable, Category="VB|LockOn") FOnTargetLockChanged OnTargetUnlocked;
protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	UPROPERTY(EditDefaultsOnly, Category="VB|LockOn") TObjectPtr<UVBTargetLockConfig> LockConfig;
private:
	UFUNCTION(Server, Reliable)
	void ServerRequestLock(AActor* Target, EVBLockReason Reason, uint32 RequestId, uint32 ExpectedRevision);
	UFUNCTION(Client, Reliable)
	void ClientLockResponse(uint32 RequestId, FVBTargetLockState State);
	UFUNCTION(Client, Unreliable)
	void ClientRecenterOnHit(AActor* Target, APlayerController* HitController, double ServerHitTime);
	// 소유 클라: control rotation yaw 와 캡슐 yaw 를 같은 프레임에 Yaw 로 맞춘다. 놓치면 눈에 보여 Reliable 이다.
	UFUNCTION(Client, Reliable)
	void ClientFaceYaw(float Yaw);
	UFUNCTION() void OnRep_LockState();
	UFUNCTION() void OnInputDeviceChanged(ECommonInputType NewDeviceType);
	// 서버: 락온을 끊을 이유가 생겼는지 프레임마다 본다.
	void TickServerLock(float DeltaTime);
	// 서버: 대상에게서 등 돌리고 달린 시간을 재서 넘으면 끊고, 아니면 전투 상태를 잇는다.
	void TickRetreatSprint(const AActor& Target, float DeltaTime);
	// 소유 클라: 카메라 추적 가능 여부와 대상 가림을 보고, 오래 가리면 해제를 요청한다.
	void TickLocalCameraTracking(float DeltaTime);
	void RequestLock(AActor* Target, EVBLockReason Reason);
	void SetLockOn(AActor* Target, EVBLockReason Reason);
	void AcceptState(const FVBTargetLockState& State);
	void RefreshOwner();
	void ResetCameraInput(bool bKeepFraming = false);
	bool HasDefeatFraming() const;
	bool IsTargetEligible(AActor* Actor) const;
	bool IsTargetValid(AActor* Actor) const;
	bool CanOwnerLock() const;
	bool CanUseCombatCamera() const;
	bool IsAutoLockSuppressed() const;
	void CancelHitRecenter(bool bSuppressNew = false);
	FVector GetAimPoint(AActor* Target) const;
	bool HasLineOfSight(AActor* Target, const FVector& Start) const;
	bool ProjectTarget(AActor* Target, FVector2D& OutScreen, bool bRequireOnScreen) const;
	// RetargetAnchor 가 있으면(대상이 죽은 자리) 화면 가운데 원뿔 대신 그 자리에서 가까운 순으로 고른다 - 처치 뒤 다음 대상.
	AActor* FindScreenTarget(float Direction, const FVector* RetargetAnchor = nullptr);
	AActor* FindBestTarget();
	AActor* FindSideTarget(float AxisValue);
	// 설정의 검색 채널로 Origin 주변의 액터를 모은다. 찾았으면 true.
	bool OverlapTargets(const FVector& Origin, float Radius, const TArray<AActor*>& Ignore, TArray<AActor*>& OutActors) const;
	// 서버의 락 시작, 끝 부수 효과 - 락을 바꾸는 길은 모두 SetLockOn 을 거치고 EndPlay 만 End 를 직접 부른다.
	//   Begin: 질주 멈춤, 전투 자극, 지금 PlayerState ASC 에 State.Combat.LockedOn 한 몫. End: 그 몫을 뗀다.
	void BeginServerLock();
	void EndServerLock();
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(ReplicatedUsing=OnRep_LockState) FVBTargetLockState LockState;
	// RPC와 속성 복제 순서가 달라도 최신 확정 상태만 소비한다.
	UPROPERTY() FVBTargetLockState ConfirmedState;
	UPROPERTY() TWeakObjectPtr<AVBCharacter> OwnerCharacter;
	// 락온을 유지하는 동안 매 프레임 전투 자극을 보내는 형제 컴포넌트 - 소유자를 다시 볼 때 함께 받는다.
	UPROPERTY() TWeakObjectPtr<UVBWeaponStateComponent> OwnerWeaponState;
	UPROPERTY() TWeakObjectPtr<APlayerController> OwnerController;
	uint32 NextRequestId = 0;
	uint32 PendingRequestId = 0;
	uint32 LastServerRequestId = 0;
	bool bSuppressCameraUntilResponse = false;
	// 대상이 죽어 다음 틱에 새 대상을 요청해야 한다(요청 전까지만 참).
	bool bDeathReacquirePending = false;
	// 락온 대상이 죽은 자리(AcceptState 가 죽음 상태를 받을 때 옛 대상에서 읽는다). 다음 대상을 이 자리에서 가까운 적으로 고른다.
	FVector DeathRetargetAnchor = FVector::ZeroVector;
	bool bHasDeathRetargetAnchor = false;
	// 서버: Begin 뒤 End 전. 대상이 파괴돼도 풀 것이 있는지 이것으로 묻는다(ASC 가 없어 태그를 못 붙였어도 참).
	bool bServerLockActive = false;
	// State.Combat.LockedOn 한 몫의 영수증(서버) - 붙인 ASC 에서 뗀다(그 사이 PlayerState 를 잃었어도).
	FVBLooseTagHold LockOnTag;
	bool bHasVisibleAimPoint = false;
	bool bWasCameraTracking = false;
	FVector LastVisibleAimPoint = FVector::ZeroVector;
	FVector2D LookOffset = FVector2D::ZeroVector;
	float OccludedSeconds = 0.0f;
	float VisibilityCheckElapsed = 0.0f;
	bool bTargetVisible = true;
	double LastLookInputTime = -1.0;
	double SuppressHitUntil = -1.0;
	float RetreatSprintSeconds = 0.0f;
	VBTargetLockPolicy::FStickLatch SwitchLatch;
	VBTargetLockPolicy::FHitRecenterWindow HitRecenterWindow;
	TWeakObjectPtr<AActor> HitRecenterTarget;
	FRotator HitRecenterStartRotation = FRotator::ZeroRotator;
	FRotator HitRecenterDelta = FRotator::ZeroRotator;
	double LastReceivedHitTime = -DBL_MAX;
	FVBTargetLockFraming CameraFraming;
	TWeakObjectPtr<AActor> CameraFramingTarget;
	uint64 NextCameraFramingId = 0;
	uint64 CameraFramingId = 0;
	double DefeatFramingUntil = -1.0;
};
