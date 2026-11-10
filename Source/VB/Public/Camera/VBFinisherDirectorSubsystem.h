// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "VBFinisherDirectorSubsystem.generated.h"

class AVBFinisherCamera;
class APlayerController;
class APawn;
class UContextualAnimSceneActorComponent;
class UVBFinisherConfig;
class UVBFinisherComponent;
struct FVBFinisherTimeDilationSegment;

// 처형 카메라를 세운 뷰어 한 명 - 컨트롤러, 세운 카메라, 그때 조종하던 폰.
USTRUCT()
struct FVBFinisherViewLease
{
	GENERATED_BODY()

	TWeakObjectPtr<APlayerController> Controller;
	UPROPERTY()
	TObjectPtr<AVBFinisherCamera> Camera = nullptr;
	TWeakObjectPtr<APawn> Pawn;
};

/**
 * 처형 연출을 보는 사람 쪽에서 세운다. 뷰타깃이 장면 참가자인 로컬 플레이어에게만 전용 카메라와 슬로우를 건다.
 * 「누가 조종하나」가 아니라 「무엇을 보나」로 물어 관전, 분할 화면도 같은 판정을 탄다. 진입점은 OnConfigArrived 하나다.
 * 로컬 플레이어가 없으면(데디 서버) 아무 일도 안 한다.
 */
UCLASS()
class VB_API UVBFinisherDirectorSubsystem : public UTickableWorldSubsystem
{
	GENERATED_BODY()

public:
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

	// 게임 월드에서만 산다. 에디터 프리뷰 월드에 카메라를 세울 이유가 없다.
	virtual bool DoesSupportWorldType(const EWorldType::Type WorldType) const override;

	// 처형 중에만 돈다 - 슬로우 구간을 씬 재생 시간에 맞춰 건다.
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual TStatId GetStatId() const override;

	// NextIndex 부터 SceneTime 까지 지난 구간 중 마지막 번호(없으면 INDEX_NONE). 같은 이름 요청은 덮어써서 마지막만 건다.
	//   구간은 Time 오름차순이다(IsDataValid).
	static int32 TakeDueSlowSegment(const TArray<FVBFinisherTimeDilationSegment>& Segments, int32& NextIndex, float SceneTime);

	// 늦게 시작한 기계가 이미 끝난 슬로우를 다시 걸지 않게 창(Time 부터 Scale*RealDuration 씬 초)이 지난 구간을 건너뛴다.
	static void SkipExpiredSlowSegments(const TArray<FVBFinisherTimeDilationSegment>& Segments, int32& NextIndex, float SceneTime);

	// 지금 도는 장면의 공격자와 그 컨트롤러/카메라 짝만 처형 강조를 받는다.
	bool IsFeedbackViewTarget(const APlayerController* Controller, const APawn* Recipient, const AActor* ViewTarget) const;
	// 안전한 구도를 잃은 뷰어만 게임플레이 시점으로 돌리고 처형은 계속한다.
	void ReleaseObstructedView(AVBFinisherCamera* Camera);

	// 장면이 그 뷰어의 화면을 아직 담당하나(순수): 폰이 그대로이고, 블렌드 중이면 그 대상이, 아니면 현재 뷰타깃이 우리 카메라.
	static bool OwnsView(const AActor* OurCamera, const AActor* CurrentTarget, const AActor* PendingTarget,
		const APawn* LeasePawn, const APawn* NowPawn);

private:
	friend class FVBCombatFeedbackFinisherViewTest;
	friend class FVBFinisherViewReleaseTest;

	// 설정이 이 기계에 도착했다. 장면이 아직이면 기다렸다가, 이미 서 있으면 그 자리에서 시작한다.
	void HandleConfigArrived(AActor* Attacker, const UVBFinisherConfig* InConfig);

	// 플러그인의 장면 진입 알림. 설정 도착과 순서가 정해지지 않아 늦게 오는 쪽이 방아쇠가 된다.
	UFUNCTION()
	void HandleJoinedScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	UFUNCTION()
	void HandleLeftScene(UContextualAnimSceneActorComponent* SceneActorComponent);

	// 두 신호가 다 모였는지 보고, 모였으면 카메라를 세운다. 두 번 불려도 한 번만 선다.
	void TryBegin();

	// 카메라와 슬로우를 거둔다. 끝나는 경로가 여럿이라(장면 종료, 설정 비움, 월드 종료) 한 곳에 모은다.
	void End();
	void RestoreViewer(const FVBFinisherViewLease& Lease, float BlendTime) const;

	// 이 기계에서 그 장면을 보고 있는 로컬 플레이어들. 뷰타깃이 참가자인 사람만 담긴다.
	void CollectViewingControllers(const TArray<AActor*>& Participants,
		TArray<APlayerController*>& OutControllers) const;

	// 설정의 CameraAnchorRole 이 가리키는 액터. 못 찾으면 nullptr 이고 카메라를 세우지 않는다.
	AActor* ResolveAnchor(const UVBFinisherComponent* Finisher) const;

	// 지금 진행 중인 처형의 공격자. 여기에 설정과 장면이 매달려 있다.
	TWeakObjectPtr<AActor> PendingAttacker;

	UPROPERTY()
	TObjectPtr<const UVBFinisherConfig> PendingConfig = nullptr;

	// 우리가 구독을 걸어 둔 씬 컴포넌트. 끊을 때 같은 것을 찾아야 하므로 들고 있는다.
	TWeakObjectPtr<UContextualAnimSceneActorComponent> WatchedScene;

	// 세운 카메라마다 뷰어 한 줄(분할 화면이면 사람마다).
	UPROPERTY()
	TArray<FVBFinisherViewLease> ViewLeases;

	// 슬로우 구간 중 아직 안 건 첫 번호. 처형마다 0 에서 시작한다.
	int32 NextSlowSegment = 0;

	// 시간 배율 요청을 걸었는가. 안 걸었는데 Pop 하면 남의 요청을 지울 수 있다.
	bool bPushedTimeDilation = false;

	// 한 장면에 한 번만 세운다. TryBegin 이 두 신호 양쪽에서 불리기 때문이다.
	bool bRunning = false;
};
