// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "VBPlayerRestoreComponent.generated.h"

class ACharacter;

DECLARE_DYNAMIC_MULTICAST_DELEGATE(FVBOnRestoreCompleted);

enum class EVBPlayerRestoreOutcome : uint8
{
	Unknown, InProgress, NotRequired, Completed,
	StreamingTimedOut, GroundNotFound, StartFailed, Cancelled,
	// 순간 이동만: 도착 자리에 서 있을 공간이 없다(FVBTeleportRequest::bRequireStandableArrival).
	Obstructed
};

/** 게임 중 순간 이동 하나. 기본은 빠른 이동이고, '자리로 옮기기' 는 붙들기, 도착 검사, 실패 시 원위치를 켠다. */
struct FVBTeleportRequest
{
	FTransform Target;
	// 이 시간 동안 지금 자리에 얼려 둔 뒤 옮긴다 - 화면이 어두워지는 동안 떨어지지 않게. 0 이면 바로 옮긴다.
	float HoldSeconds = 0.0f;
	// 도착 자리가 걸을 수 있는 바닥이고 서 있을 공간이 있어야 Completed 다. 아니면 GroundNotFound / Obstructed.
	bool bRequireStandableArrival = false;
	// Completed 가 아니면(바닥 없음, 막힘, 스트리밍 시간 초과, 취소) 고정해 둔 위치로 되돌린다 - 위험한 표시 위치에 내려놓지 않는다.
	bool bReturnOnFailure = false;
};

DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnTeleportFinished, FGuid /*OperationId*/, EVBPlayerRestoreOutcome /*Outcome*/);

struct FVBPlayerRestoreReceipt
{
	FGuid InitializationId;
	FGuid OperationId;
	EVBPlayerRestoreOutcome Outcome = EVBPlayerRestoreOutcome::Unknown;
	bool bStreamingTimedOut = false;
	bool bGroundFound = false;
	bool IsSafeForPreparation() const
	{
		return Outcome == EVBPlayerRestoreOutcome::NotRequired
			|| (Outcome == EVBPlayerRestoreOutcome::Completed && bGroundFound && !bStreamingTimedOut);
	}
};

/**
 * 저장된 위치로 플레이어를 World Partition 스트리밍에 안전하게 복원한다(위치 구역의 저장 참여자).
 * 지형이 뜨기 전 낙하하지 않게 폰을 동결한 채 IsStreamingCompleted 를 기다린 뒤 지면에 스냅한다.
 * WP 를 쓰지 않는 맵이면 지면 스냅만 한다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBPlayerRestoreComponent : public UActorComponent, public IVBSaveParticipant
{
	GENERATED_BODY()

public:
	UVBPlayerRestoreComponent();

	//~ IVBSaveParticipant - 위치 구역(Progress.SavedPlayerTransform). 문맥의 폰이 이 컴포넌트의 소유자일 때만.
	virtual void WriteSave(UVBSaveGame& Save, const FVBSaveContext& Context) override;
	virtual void ReadSave(const UVBSaveGame& Save, const FVBSaveContext& Context) override;

	void ArmInitialization(FGuid InitializationId);
	void MarkRestoreNotRequired(FGuid InitializationId);
	bool IsRestoring() const { return bRestoring; }
	const FVBPlayerRestoreReceipt& GetRestoreReceipt() const { return RestoreReceipt; }
	FSimpleMulticastDelegate OnRestoreStateChanged;

protected:
	// 등록은 BeginPlay 가 아니라 여기 - Continue 적용은 폰이 스폰된 직후, 월드 BeginPlay 전에 온다(IVBSaveParticipant 주석).
	virtual void OnRegister() override;
	virtual void OnUnregister() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:

	// 저장된 Transform 으로 스트리밍-안전 복원을 시작한다. 소유 액터는 APawn(ACharacter) 이어야 한다.
	UFUNCTION(BlueprintCallable, Category="VB|Save")
	void BeginRestore(const FTransform& Target);

	// 게임 중 순간 이동. 복원과 같은 길(스트리밍 대기 -> 지면 스냅 -> OnRestoreCompleted)이지만 불러오기 영수증은 건드리지 않는다.
	//   시작하면 작업 ID, 못 하면(진행 중, 소유 액터 없음) 빈 ID - 그때는 OnTeleportFinished 도 오지 않는다.
	FGuid BeginTeleport(const FVBTeleportRequest& Request);

	// 그 작업이 아직 진행 중이면 끝낸다(되돌리기 요청이면 얼린 자리로). 결과 Cancelled 가 OnTeleportFinished 로 간다. 다른 작업 ID 는 무시한다.
	void CancelTeleport(FGuid OperationId);

	// 순간 이동 하나가 끝날 때마다 정확히 한 번(시작 뒤의 실패, 취소, EndPlay 포함). 부르는 쪽은 작업 ID 로 제 것만 받는다.
	FVBOnTeleportFinished OnTeleportFinished;

	// 복원 완료(지면 스냅 + 동결 해제) 시 브로드캐스트. 순간 이동이 지면 스냅까지 간 때도 온다(플레이 봇의 빠른 이동 관찰이 쓴다).
	UPROPERTY(BlueprintAssignable, Category="VB|Save")
	FVBOnRestoreCompleted OnRestoreCompleted;

	// 스트리밍 완료 대기 최대 시간(초). 초과 시 강제로 지면 스냅으로 진행(무한 대기 방지).
	UPROPERTY(EditDefaultsOnly, Category="VB|Save")
	float StreamingTimeoutSeconds = 10.0f;

	// 스트리밍 완료 폴링 주기(초). SetTimer 가 0 이하를 해제로 읽으므로 런타임 하한도 따로 둔다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Save", meta=(ClampMin="0.02"))
	float PollIntervalSeconds = 0.1f;

	// 지면 스냅 라인트레이스의 위쪽 시작 높이(cm, 저장 좌표 기준). 좌표가 조금 잠겨도 지면을 찾는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Save")
	float GroundSnapTraceUpDistance = 200.0f;

	// 지면 스냅 라인트레이스의 아래쪽 끝 깊이(cm, 저장 좌표 기준). 너무 짧으면 지면을 못 찾아 다시 낙하한다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Save")
	float GroundSnapTraceDownDistance = 500.0f;

	// 캡슐을 못 얻었을 때 쓸 반높이(cm, 마네킹 기본값). 방어용이다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Save")
	float FallbackCapsuleHalfHeight = 88.0f;

	// 도착 공간 검사에서 캡슐을 바닥 위로 띄우는 높이(cm). 바닥에 닿은 캡슐은 바닥과 겹친 것으로 나와서 띄운다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Save", meta=(ClampMin="0.1"))
	float ArrivalFloorClearance = 2.0f;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBPlayerRestoreTestAccess;
#endif
	void SetRestoreOutcome(EVBPlayerRestoreOutcome Outcome);
	void CancelRestoreReceipt();
	FVBPlayerRestoreReceipt RestoreReceipt;
	FGuid RunningInitializationId;
	FGuid RunningOperationId;
	bool bStreamingTimedOut = false;
	bool bEnding = false;
	// 복원, 순간 이동의 공통 시작. bWriteReceipt = 불러오기 영수증에 결과를 쓰나(복원만). 진행 중이면 거절한다.
	bool StartMove(const FVBTeleportRequest& Request, bool bWriteReceipt);

	// 붙들기가 끝나면(또는 붙들기 없이 바로) 목표 자리에 놓고 스트리밍 폴링을 시작한다.
	void PlaceAndPoll();

	// 스트리밍 완료 여부를 주기적으로 확인. 완료/타임아웃 시 지면 스냅으로 마무리.
	void PollStreaming();

	// 지면으로 라인트레이스해 폰을 정렬하고 동결을 해제, 완료를 통지.
	void SnapToGroundAndFinish();

	// 지면 스냅 없이 끝낸다(취소, 월드 없음). 되돌리기 요청이면 얼린 자리로, 월드 해체, 폰 파괴 중이면 놓지 않는다.
	void EndWithoutPlacement(EVBPlayerRestoreOutcome Outcome);

	// 순간 이동이면 OnTeleportFinished 를 한 번 알리고 그 표시를 내린다. 복원이면 아무것도 안 한다.
	void NotifyTeleportFinished(EVBPlayerRestoreOutcome Outcome);

	// 도착 자리에 캡슐이 설 공간이 있나(바닥 위 ArrivalFloorClearance). 폰 자신은 뺀다.
	bool HasStandingRoom(const ACharacter& Pawn, const FVector& Location) const;

	// 폰 물리/이동/콜리전을 얼리거나(true) 되돌린다(false). 복원 중 낙하/관통 방지.
	void FreezePawn(bool bFreeze);

	// 복원 목표 Transform(폴링 완료 후 지면 스냅의 기준).
	FTransform PendingTarget;

	// 지금 작업이 순간 이동이면 그 요청(붙들기, 도착 검사, 되돌리기). 복원이면 bTeleporting 이 거짓이다.
	FVBTeleportRequest ActiveTeleport;
	bool bTeleporting = false;

	// 얼린 순간의 자리 - 목표에 놓기 전에 잡는다(실패한 순간 이동이 돌아갈 곳).
	FTransform HoldTransform;
	FTimerHandle HoldTimerHandle;

	// 폴링 타이머 핸들 + 시작 시각(타임아웃 판정).
	FTimerHandle PollTimerHandle;
	double StreamStartTime = 0.0;

	// 동결 전 원래 GravityScale(해제 시 복원). 캐릭터가 아닐 때 대비 기본 1.
	float SavedGravityScale = 1.0f;

	// 복원 진행 중 재진입 방지.
	bool bRestoring = false;
};
