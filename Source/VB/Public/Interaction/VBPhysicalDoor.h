// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBTimedMove.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "StructUtils/InstancedStruct.h"
#include "VBPhysicalDoor.generated.h"

class APlayerState;
class AVBAreaTrigger;
class UStaticMeshComponent;
class UVBInteractableComponent;

UENUM(BlueprintType)
enum class EVBDoorMotion : uint8
{
	// 경첩(Hinge) 의 위쪽 축을 중심으로 OpenAngle 만큼 돈다.
	Swing,
	// 경첩 공간에서 SlideOffset 만큼 미끄러진다.
	Slide,
};

UENUM(BlueprintType)
enum class EVBDoorMoveResult : uint8
{
	// 목표 끝 자세에 정확히 닿았다.
	Reached,
	// 문짝이 무언가와 맞닿아 그 앞에서 멈췄다.
	Blocked,
	// 더 새 요청, 즉시 자세, EndPlay 가 이 요청을 끝냈다.
	Cancelled,
};

// 서버에서만. 받아들인 요청(작업 번호)마다 정확히 한 번. 상태를 다 바꾼 뒤에 알린다 - 듣는 쪽이 안에서 새 요청을 해도 된다.
DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnDoorMoveFinished, uint32 /*Serial*/, EVBDoorMoveResult /*Result*/);

/**
 * 닿으면 멈추는 문 - 서버가 문짝을 한 걸음씩 옮기며 지나갈 자리를 먼저 재고, 서로 막는 것이 있으면 그 앞에서 멈춘다.
 * 저장되는 것은 마지막으로 닿은 끝(Interactable 의 bActivated)이라 움직이는 중에 저장해도 실제로 서 있던 끝이 남는다.
 * 이야기 닫힘(ClosedWhile, ClosedWhileOccupied)을 적으면 문이 그 답을 스스로 따르고 막히면 다시 간다. 비우면 요청이 올 때만 움직인다.
 */
UCLASS()
class VB_API AVBPhysicalDoor : public AActor
{
	GENERATED_BODY()

public:
	AVBPhysicalDoor();

	// 서버에서만. 지금 자리에서 그 끝 자세로 움직이고 작업 번호를 준다(진행 중 요청은 Cancelled). 거절이면 0 + OutRefusal, 아무것도 안 바뀐다.
	// 이미 그 끝이면 받아들이고 Reached 는 다음 틱에 알린다(부른 쪽이 번호를 받아 둔 뒤에).
	uint32 RequestMove(bool bOpen, FString* OutRefusal = nullptr);

	// 서버에서만. 막힘 판정 없이 그 끝 자세에 바로 둔다(복원, 완료 재구성). 진행 중인 요청은 Cancelled, 이 호출 자체는 알림이 없다.
	void SetPoseInstant(bool bOpen);

	// 마지막으로 닿은 끝이 열림인가(= 저장되는 값).
	bool IsOpen() const;
	// 막혀 멈춰 있나.
	bool IsBlocked() const { return Move.bStopped; }
	bool IsMoving() const;
	// 배치 결함(문짝 메시, 충돌 없음, 여행, 속도 잘못, 시작 때 길이 막힘)이면 모든 요청을 거절한다.
	bool HasFault() const { return !FaultReason.IsEmpty(); }
	const FString& GetFaultReason() const { return FaultReason; }

	UVBInteractableComponent* GetInteractable() const { return Interactable; }

	// 이야기 닫힘 칸 중 하나라도 적혔나 - 적혔으면 문을 모는 쪽은 이 문 하나여야 한다.
	bool HasStoryControl() const;

	FVBOnDoorMoveFinished OnDoorMoveFinished;

	// 순수 계산(시험용): 경첩 공간 상자를 위쪽 축으로 Angle0 -> Angle1(도) 돌릴 때 전 구간을 담는 축 정렬 상자.
	static void SweptSwingBox(const FVector& Center, const FVector& Extent, float Angle0, float Angle1, FVector& OutCenter,
		FVector& OutExtent, float& OutMidAngle);

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void PostInitializeComponents() override;
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Door")
	TObjectPtr<USceneComponent> Frame;

	// 문짝의 축. Swing 은 이 컴포넌트의 위쪽 축으로 돌고, Slide 는 이 공간에서 미끄러진다. 비균일 크기는 여기가 아니라 Leaf 에만 준다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Door")
	TObjectPtr<USceneComponent> Hinge;

	// 움직이는 문짝. Pawn 을 막아야 하고, 막힘은 이 메시 경계 상자 하나로 재므로 Hinge 에 다른 컴포넌트를 붙이지 않는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Door")
	TObjectPtr<UStaticMeshComponent> Leaf;

	// 손으로 여닫는 문의 프롬프트. 제어함이 모는 문은 배치에서 상호작용, 저장을 끈다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Door")
	TObjectPtr<UVBInteractableComponent> Interactable;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door")
	EVBDoorMotion Motion = EVBDoorMotion::Swing;

	// 열린 끝의 각(도). 부호가 도는 쪽이다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(EditCondition="Motion==EVBDoorMotion::Swing"))
	float OpenAngle = 90.0f;

	// 열린 끝까지의 이동(경첩 공간, cm).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(EditCondition="Motion==EVBDoorMotion::Slide"))
	FVector SlideOffset = FVector(0.0, 120.0, 0.0);

	// Swing 은 도/초, Slide 는 cm/초.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(ClampMin="1.0"))
	float Speed = 90.0f;

	// 한 걸음을 담는 상자가 걸음 시작의 문짝보다 커져도 되는 여유(cm). 작을수록 걸음을 잘게 나눈다. 문이 이만큼 일찍 설 수는 있어도 늦게 서지는 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(ClampMin="0.1"))
	float SweepTolerance = 1.0f;

	// 문짝이 끝 자세에서 기대는 정적 액터(열린 문이 닿는 벽 등). 움직이는 동안 이것들만 막힘에서 뺀다.
	// 시작 때 이것들이 양 끝 RestAgainstTravelFraction 밖에서 길을 막지 않는지 확인한다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Door")
	TArray<TObjectPtr<AActor>> RestAgainst;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(ClampMin="0.0", ClampMax="1.0"))
	float RestAgainstTravelFraction = 0.1f;

	// 플레이어가 이 영역 안에 있을 때만 닫힌다 - 저장되지 않는 지금의 겹침이라 밖에서 불러오면 열려 있다. 비면 점유를 보지 않는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Door")
	TObjectPtr<AVBAreaTrigger> ClosedWhileOccupied;

	// 전부 참이고 점유도 맞을 때 닫힌다. 이 칸과 위 영역이 다 비면 이야기를 보지 않는다.
	UPROPERTY(EditInstanceOnly, BlueprintReadOnly, Category="VB|Door",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> ClosedWhile;

	// 막힌 뒤 다시 가 보는 간격(초). 막은 것이 비키면 곧 닿되 매 틱 쓸기를 다시 재지 않으려고 둔다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(ClampMin="0.05"))
	float BlockedRetrySeconds = 0.5f;

	// 시작 때 사실 저장소, 점유 영역을 기다리는 상한(초). 지나면 갇히지 않게 오류를 남기고 연다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Door", meta=(ClampMin="0.1"))
	float StoryStartupTimeoutSeconds = 5.0f;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBPhysicalDoorTestAccess;
#endif
	UFUNCTION()
	void OnRep_Move();

	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UPROPERTY(ReplicatedUsing=OnRep_Move)
	FVBTimedMove Move;

	// 설정, 충돌을 본다. 결함이면 이유.
	FString ValidateSetup() const;
	// 서버, 시작 때: 정적 지형이 여행을 막지 않나, RestAgainst 가 끝 근처에서만 닿나.
	FString CheckStaticPath() const;

	float CurrentAlpha() const;
	float TravelDistance() const;
	void PlaceLeaf(float Alpha);
	FTransform HingeRelativeAt(float Alpha) const;
	// 경첩 공간의 문짝 상자(메시 경계 상자를 문짝의 경첩 기준 변환으로 옮긴 축 정렬 상자).
	void LeafBoxInHinge(FVector& OutCenter, FVector& OutExtent) const;
	// 알파 A0 -> A1 사이를 지나는 문짝이 무언가와 서로 막는가. bStaticOnly = 정적 지형만, IgnoreRestAgainst = 기대는 액터 빼기.
	bool IsIntervalBlocked(float A0, float A1, bool bStaticOnly, bool bIgnoreRestAgainst) const;
	// 한 걸음의 알파 폭 상한(여유 SweepTolerance 를 지키는).
	float MaxStepAlpha() const;

	// 서버: 받아들인 요청을 결과와 함께 끝낸다(상태를 바꾼 뒤 알림). 이미 끝났으면 아무것도 안 한다.
	void Finish(EVBDoorMoveResult Result);
	void RestAt(float Alpha);

	// 문짝의 배치 자리(알파 0)의 경첩 상대 변환. 움직이면 Hinge 의 상대 변환을 덮어쓰므로 BeginPlay 에서 잡아 둔다.
	FTransform HingeRest;
	uint32 NextSerial = 0;
	uint32 PendingSerial = 0;
	float LastPlacedAlpha = 0.0f;
	FString FaultReason;
	FTimerHandle ZeroDistanceTimer;

	// 서버, 시작 때: 이야기 닫힘을 묶는다. 결함이면 바로 연다.
	void StartStoryControl();
	// 사실 저장소, 점유 영역이 준비됐으면 묶고 바라는 자세에 바로 둔다(알림 없음).
	bool TryStartStory();
	void HandleStoryStartupTick();
	void HandleStoryChanged();
	// 바라는 자세와 지금을 견주어 필요할 때만 요청한다. 막혀 다시 갈 차례를 기다리는 중이면 그대로 둔다.
	void ReconcileStory();
	void HandleStoryMoveFinished(uint32 Serial, EVBDoorMoveResult Result);
	void HandleBlockedRetry();
	// 이야기 닫힘을 그만 따르고 오류 한 줄을 남긴 뒤 연다 - 따를 수 없는 문이 플레이어를 가두지 않게.
	void FailStoryOpen(const TCHAR* Why);
	void StopStoryControl();
	// 점유(영역이 없으면 참)와 ClosedWhile 이 모두 참인가.
	bool WantsClosed() const;
	// 지금 플레이어 폰 중 하나라도 영역 안에 있나 - 매번 다시 묻고 담아 두지 않는다.
	bool IsPlayerInsideArea() const;
	APlayerState* FirstPlayerState() const;

	FVBStoryStateBinding StoryBinding;
	TWeakObjectPtr<AVBAreaTrigger> BoundArea;
	FDelegateHandle AreaOccupancyHandle;
	FDelegateHandle StoryMoveHandle;
	FTimerHandle StoryStartupTimer;
	FTimerHandle BlockedRetryTimer;
	double StoryStartupDeadline = 0.0;
	bool bStoryStarted = false;
	bool bStoryFailed = false;
	bool bBlockedRetryPending = false;
	// 막힘 경고는 끝에 닿기 전까지 한 번만 낸다.
	bool bBlockedWarned = false;
};
