// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Traversal/VBLadderRuntime.h"
#include "Traversal/VBTraversalRules.h"
#include "VBLadderMode.generated.h"

class AActor;
class AVBCharacter;
class AVBLadder;
class IVBLadderModeHost;
class UActorComponent;
class UAnimInstance;
class UAnimMontage;
class USkeletalMeshComponent;
class UVBClimbAnimSet;
class UWorld;
struct FTickFunction;
struct FVBTraversalSession;

// 사다리 탑승 한 번의 처리: 탐색, 시작, 의도(A), 배치(B) 단계, 꼭대기 올라서기, 종료.
//   소유 컴포넌트는 RPC 를 맡고 IVBLadderModeHost 로 요청에 응답한다.
//   틱 의존 기록이 소유자의 틱 함수를 가리키므로 복사할 수 없다.
USTRUCT()
struct VB_API FVBLadderMode
{
	GENERATED_BODY()

	FVBLadderMode() = default;
	FVBLadderMode& operator=(const FVBLadderMode&) = delete;

	// 앞쪽에서 마주 보는 사다리를 찾아 시작 요청을 보낸다. 못 찾으면 false.
	bool TryStart(IVBLadderModeHost& Host);
	// 클라 시작 요청을 서버가 다시 검사한다. 통과면 OutHeight 에 보정한 높이.
	bool AcceptServerStart(const IVBLadderModeHost& Host, AVBLadder* Ladder, float ClientHeight, float& OutHeight) const;
	void Start(IVBLadderModeHost& Host, FVBTraversalSession& Session, AVBLadder* Ladder, float Height);
	// 놓기, 바닥 내리기, 사다리 소멸 시 호스트의 복구로 세션을 끝낸다.
	void End(IVBLadderModeHost& Host);
	// 탑승 플래그를 끄고 탑승 상태를 정리한다. 세션과 막는 상자 무시는 유지한다(올라서기가 복구까지 사용).
	void ClearRiding(IVBLadderModeHost& Host);
	// 최종 복구에서 이 모드가 추가한 막는 상자 무시를 해제하고 올라서기 지지 요청을 지운다.
	void ReleaseOnRecover(IVBLadderModeHost& Host);

	// A 단계(CMC 이전): 종료, 내리기, 올라서기 판정, 입력, 재생 속도, 몽타주 선택.
	void UpdateIntent(IVBLadderModeHost& Host);
	// B 단계(CMC 이후): CMC 가 진행한 몽타주 구간만큼 높이를 올리고 가로대 정렬 후 캡슐을 배치한다.
	void UpdatePlacement(IVBLadderModeHost& Host, float DeltaTime);

	// 소유 클라이언트 입력을 적용하고 서버로 보낸다.
	void SetMoveInput(IVBLadderModeHost& Host, float InputY);
	// 입력 RPC 의 서버 쪽. 적용만 한다.
	void ApplyMoveInput(float InputY);
	void RequestDrop(IVBLadderModeHost& Host);
	// 올라서기 멀티캐스트 수신: IK 노드에 마지막 지지를 요청하고 탑승을 정리한 뒤 올라서기 몽타주를 재생한다.
	void StartTopOut(IVBLadderModeHost& Host, const FVector& LedgePoint, const FVector& LedgeNormal);

	const FVBLadderRuntime& GetState() const { return State; }

private:
	struct FTickEdges
	{
		UObject* Owner = nullptr;
		FTickFunction* IntentTick = nullptr;
		FTickFunction* PlacementTick = nullptr;
		TWeakObjectPtr<AActor> Controller;
		TWeakObjectPtr<UActorComponent> Movement;
		TWeakObjectPtr<USkeletalMeshComponent> Mesh;
		bool bIntentAfterController = false;
		bool bMovementAfterIntent = false;
		bool bPlacementAfterMovement = false;
		bool bMeshAfterPlacement = false;
	};

	// 탑승 중 입력 -> A -> CMC -> B -> 메시 순서를 건다. 직접 추가한 의존만 기록한다.
	void BindTickOrder(IVBLadderModeHost& Host);
	// 기록한 의존을 제거하고 A 단계를 끈다.
	void UnbindTickOrder();

	UAnimMontage* PickIdle(const IVBLadderModeHost& Host) const;
	// HeightNow 에서 시작할 때 발이 가로대 윗면에 닿는 시작 위상을 반환한다. 정할 수 없으면 0.
	float FindMontageStart(const IVBLadderModeHost& Host, UAnimMontage* Montage, float HeightNow) const;
	FTransform ComputeCapsuleTransform(const IVBLadderModeHost& Host, float Height) const;
	// 붙는 블렌드의 진행도(0..1). 배치와 올라서기의 붙음 완료 판정이 같이 사용한다.
	float GetEntryAlpha(const IVBLadderModeHost& Host) const;
	FVBTopOutResult FindTopOut(const IVBLadderModeHost& Host) const;

	// 꼭대기에서 위로 밀면 서버가 올라서기를 확정한다. 확정했으면 true.
	bool TryTopOutAtLimit(IVBLadderModeHost& Host, const AVBCharacter& Rider, const AVBLadder& Ladder, float Want, float Top);
	// 입력 방향의 몽타주를 고른다. 입력이 없으면 대기 몽타주다.
	UAnimMontage* PickWantedMontage(const IVBLadderModeHost& Host, const UVBClimbAnimSet& Clips, float Want) const;
	// 고른 몽타주를 알맞은 위상에서 재생하고 재생 속도를 맞춘다.
	void DriveWantedMontage(const IVBLadderModeHost& Host, const UVBClimbAnimSet& Clips, UAnimInstance* Anim, UAnimMontage* Wanted,
		float PlayRate);

	// 배치가 A 단계와 다른 프레임이거나 루트모션 상태가 달라졌으면 경고한다.
	void WarnIfOutOfIntentOrder(const UWorld& World, bool bRootMotionAtPlace);
	// 지난 위상부터 지금 위상까지의 루트모션을 소비해 축 위 이동량을 돌려주고 위상을 기록한다.
	float ConsumeMontageRootMotion(UAnimInstance* Anim, const USkeletalMeshComponent* Mesh, const UVBClimbAnimSet& Clips,
		const FVector& Axis, FVector& OutRootMotionMoved);
	// 이동량을 상한으로 잘라 높이에 더하고 실제로 적용한 양을 돌려준다.
	float AdvanceHeight(const UVBClimbAnimSet& Clips, const UWorld& World, float Along, float DeltaTime);
	// 입력이 없는 동안 가장 가까운 가로대로 당겨 앉는다.
	void UpdateIdleSettle(const AVBLadder& Ladder, const UVBClimbAnimSet& Clips, float DeltaTime);
	// 보이는 자리가 클립 속도를 넘게 뛰었으면 항목별로 찍는다.
	void ReportVisibleJump(const AVBCharacter& Rider, const UWorld& World, const FVector& Location, const FVector& RootMotionMoved,
		const FVector& Axis, float Along, float DeltaTime);
	// 콘솔 변수가 켜져 있으면 배치 결과를 매 프레임 찍는다.
	void LogPlacementTelemetry(const IVBLadderModeHost& Host, const UWorld& World, const USkeletalMeshComponent* Mesh, float ConsumedFrom,
		float Along, bool bRootMotionAtPlace) const;

	UPROPERTY()
	FVBLadderRuntime State;

	FTickEdges TickEdges;
};

template<>
struct TStructOpsTypeTraits<FVBLadderMode> : public TStructOpsTypeTraitsBase2<FVBLadderMode>
{
	enum { WithCopy = false };
};
