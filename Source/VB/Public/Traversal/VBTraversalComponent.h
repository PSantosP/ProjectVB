// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Traversal/VBTraversalTypes.h"
#include "Animation/AnimMontage.h" // FOnMontageEnded
#include "Engine/HitResult.h"     // CapsuleSweep 등 선언 - 공용 PCH 에 기대지 않는다(PCH 없는 -SingleFile 확인에서 C2061)
#include "Traversal/VBClimbMode.h"
#include "Traversal/VBHangMode.h"
#include "Traversal/VBLadderMode.h"
#include "Traversal/VBLadderModeHost.h"
#include "Traversal/VBTraversalSession.h"
#include "VBTraversalComponent.generated.h"

class AVBLadder;

class AVBCharacter;
class UVBTraversalConfig;
class UVBClimbAnimSet;
class UPrimitiveComponent;
class USkeletalMeshComponent;
class UVBTraversalComponent;
struct FPoseSearchBlueprintResult;

// 사다리 A 단계 틱. CMC 보다 먼저 FVBLadderMode::UpdateIntent 를 호출한다. 배치(B)는 주 틱이 CMC 다음에 한다.
USTRUCT()
struct FVBLadderIntentTickFunction : public FTickFunction
{
	GENERATED_BODY()

	UVBTraversalComponent* Target = nullptr;

	virtual void ExecuteTick(float DeltaTime, ELevelTick TickType, ENamedThreads::Type CurrentThread,
		const FGraphEventRef& MyCompletionGraphEvent) override;
	virtual FString DiagnosticMessage() override;
	virtual FName DiagnosticContext(bool bDetailed) override;
};

template<>
struct TStructOpsTypeTraits<FVBLadderIntentTickFunction> : public TStructOpsTypeTraitsBase2<FVBLadderIntentTickFunction>
{
	enum { WithCopy = false };
};

/**
 * 트래버설(맨틀, 볼트, 허들)과 세션 복구, RPC, 입력 진입점, 공개 조회를 맡는 컴포넌트. GASP 트래버설의 서버 권위 C++ 포팅이다.
 * 매달림, 등반, 사다리 본문은 FVBHangMode, FVBClimbMode, FVBLadderMode 가 맡고, 세션이 빌린 것은 FVBTraversalSession 이 돌려준다.
 * 매달림, 등반, 사다리 중에는 캡슐 위치와 회전을 이 컴포넌트가 제어한다(MOVE_Flying, 속도 0).
 */
UCLASS(ClassGroup=(Custom), meta=(BlueprintSpawnableComponent))
class VB_API UVBTraversalComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBTraversalComponent();
	// AVBCharacter 가 만든다. 없으면 매달림, 등반, 사다리를 하지 않는 액터다.
	static UVBTraversalComponent* FindFor(const AActor* Actor);

	/** 점프 입력 진입점. 트래버설을 했으면 true(점프 억제). bAirMantleOnly 면 공중이라 맨틀만 허용. */
	UFUNCTION(BlueprintCallable, Category="VB|Traversal")
	bool TryTraversalAction(bool bAirMantleOnly = false);

	FORCEINLINE bool IsDoingTraversal() const { return TraversalPhase != EVBTraversalPhase::None; }
	FORCEINLINE EVBTraversalPhase GetTraversalPhase() const { return TraversalPhase; }

	// 면에 붙어 있으면 이동 입력을 그 모드로 보내고 true. false 면 호출자가 CMC 로 보낸다.
	bool TryConsumeMoveInput(const FVector2D& Input);

	// === 매달림 ===
	FORCEINLINE bool IsHanging() const { return AttachedSurface == EVBTraversalSurface::Hang; }
	// 옆걸음 애니 축 -1(왼쪽)~+1(오른쪽). UVBAnimInstance 가 미러해 BS_VB_LedgeHang 의 X 로 쓴다.
	FORCEINLINE float GetHangMoveAxis() const { return HangMode.GetState().MoveAxis; }

	// 매달림 IK 목표(월드). ABP 는 AnimInstance 를 거쳐 읽기만 한다.
	FORCEINLINE const FVBHangIKTargets& GetHangIKTargets() const { return HangMode.GetState().IKTargets; }

	// 매달림 중 이동 입력. X=좌우 옆걸음, Y=앞(올라서기)/뒤(놓기).
	void SetHangMoveInput(const FVector2D& Input);
	// 점프 -> 올라서기, 크라우치 -> 놓기. 소유 클라가 부르고 서버가 전원에 퍼뜨린다.
	void RequestHangClimbUp();
	void RequestHangDrop();

	// === 자유 등반 ===
	FORCEINLINE bool IsClimbing() const { return AttachedSurface == EVBTraversalSurface::Climb; }

	// 사다리에 붙어 있나. 애니는 UVBAnimInstance 가 미러한다.
	FORCEINLINE bool IsOnLadder() const { return AttachedSurface == EVBTraversalSurface::Ladder; }

	// 이 컴포넌트가 캡슐 회전을 제어하는가. 이동 컴포넌트가 이것을 보고 CMC 회전 플래그를 끈다(제어 주체가 둘이 되지 않게).
	FORCEINLINE bool OwnsCapsuleFacing() const { return AttachedSurface != EVBTraversalSurface::None; }

	// 사다리 IK 노드가 PreUpdate(게임 스레드)에서 기하/설정만 복사한다.
	const AVBLadder* GetActiveLadderForIK() const { return IsOnLadder() ? LadderMode.GetState().ActiveLadder.Get() : nullptr; }
	// 꼭대기 올라서기 동안 IK 노드가 마지막 지지를 이어 받는 사다리. bOutRequested=false 면 요청 없음.
	const AVBLadder* GetLadderTopOutSupportForIK(bool& bOutRequested) const
	{
		const FVBLadderTopOutSupport& Support = LadderMode.GetState().TopOutSupport;
		bOutRequested = Support.bRequested;
		return Support.bRequested ? Support.Ladder.Get() : nullptr;
	}

	const UVBClimbAnimSet* GetClimbAnimSetForIK() const { return ClimbAnimSet; }
	// IK 노드가 접촉 문턱을 이 몽타주 배속으로 환산한다. 다른 슬롯 몽타주와 섞이지 않게 사다리 것을 직접 준다.
	const UAnimMontage* GetActiveLadderMontageForIK() const { return IsOnLadder() ? LadderMode.GetState().ActiveMontage.Get() : nullptr; }

	// 사다리에서 놓기.
	UFUNCTION(BlueprintCallable, Category="VB|Traversal")
	void RequestLadderDrop();

	// 사다리 위아래 입력(축 하나).
	void SetLadderMoveInput(float InputY);
	// 값만 보는 판정은 VBTraversalRules 가 하고, 이 컴포넌트는 입력을 모아 부르고 결과를 적용한다.

	// 질주 자동 진입. 소유 클라 Tick 이 부르며 주기마다 TryTraversalAction 을 시도한다(점프 폴백 없음).
	void TryAutoTraversal();
	// 등반 블렌드스페이스의 두 축. X=-1(왼쪽)~+1(오른쪽), Y=-1(아래)~+1(위).
	FORCEINLINE FVector2D GetClimbMoveAxis() const { return ClimbMode.GetState().MoveAxis; }
	// 등반 중 네 말단의 IK 목표와 세기. 매달림과 같은 구조체지만 등반에서는 뼈 공간 오프셋이다.
	FORCEINLINE const FVBHangIKTargets& GetClimbIKTargets() const { return ClimbMode.GetState().IKTargets; }

	// 소유 클라의 이동 입력을 등반이 가로챈다. X=좌우, Y=위아래.
	void SetClimbMoveInput(const FVector2D& Input);
	// 크라우치 입력 -> 벽에서 놓기.
	void RequestClimbDrop();

	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

protected:
	virtual void BeginPlay() override;
	// 사다리 A 단계 틱을 컴포넌트와 같은 수명으로 등록, 해제한다. 켜고 끄는 것은 FVBLadderMode 다.
	virtual void RegisterComponentTickFunctions(bool bRegister) override;

	// 트레이스, 판정 임계값.
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal")
	TObjectPtr<UVBTraversalConfig> Config;

	// 매달림, 사다리, 등반의 클립과 자세 기하.
	//   비어 있으면 매달림은 꺼지고 사다리, 등반은 첫 갱신에서 끝난다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal")
	TObjectPtr<UVBClimbAnimSet> ClimbAnimSet;

	// ABP PoseSearchHistoryCollector 노드 태그. 틀리면 MotionMatch 가 실패해 트래버설이 안 걸린다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Traversal")
	FName PoseHistoryName = TEXT("PoseHistory");

	// 설정. 미할당이면 CDO 기본값.
	const UVBTraversalConfig* GetConfig() const;

	// 전방, 공간, 높이, 깊이, 바닥 트레이스로 Result 를 채운다. 전방 렛지가 있으면 true. bAirMode 면 전방 거리를 고정 단거리로.
	bool PerformTraversalCheck(FVBTraversalCheckResult& OutResult, bool bAirMode = false) const;

	// 검사 결과와 설정으로 동작 종류를 정한다.
	EVBTraversalActionType DecideActionType(const FVBTraversalCheckResult& R) const;

	// === 서버 권위 래퍼 ===
	// 서버는 클라가 보낸 렛지 좌표를 캐릭터 위치 기준 거리 상한으로 검증한다(임의 워프 차단).
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	UFUNCTION(NetMulticast, Reliable)
	void MulticastPerformTraversal(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// Flying 전환, 장애물 콜리전 무시, 워프, 몽타주 재생.
	void PerformTraversalAction(const FVBTraversalCheckResult& Result, UAnimMontage* Montage, float StartTime, float PlayRate);

	// 몽타주 재생과 복구 델리게이트 바인딩. 재생을 못 걸면 바로 복구하고 false(콜백을 기다리며 굳지 않게).
	bool PlayTraversalMontage(UAnimMontage* Montage, float StartTime, float PlayRate);

	// === 매달림 RPC. 본문은 FVBHangMode ===
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartHang(const FVBTraversalCheckResult& Result);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartHang(const FVBTraversalCheckResult& Result);

	// 옆걸음 입력. 위치는 서버가 굴리고 CMC 복제가 나른다. 자주 보내서 Unreliable.
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetHangMoveInputX(float InputX);

	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHangClimbUp();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHangClimbUp();
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerHangDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastHangDrop();

	// 앞쪽에서 마주 보는 사다리를 찾아 붙으면 true. 등반보다 먼저 확인한다.
	bool TryStartLadder();

	// === 자유 등반 RPC. 본문은 FVBClimbMode ===
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartClimb(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartClimb(FVector SurfacePoint, FVector SurfaceNormal, UPrimitiveComponent* Surface);
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetClimbMoveInput(FVector2D Input);
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerClimbDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastClimbDrop();
	// 꼭대기 올라서기. 서버가 찾은 렛지만 보내므로 Server RPC 가 없다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastClimbTopOut(FVector LedgePoint, FVector LedgeNormal);

	// 사다리도 서버가 상태를 관리하고 소유 클라이언트는 축 값만 보낸다.
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerStartLadder(AVBLadder* Ladder, float Height);
	UFUNCTION(NetMulticast, Reliable)
	void MulticastStartLadder(AVBLadder* Ladder, float Height);
	UFUNCTION(Server, Unreliable, WithValidation)
	void ServerSetLadderMoveInput(float InputY);
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerLadderDrop();
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLadderDrop();
	// 사다리 꼭대기 올라서기. 서버가 찾은 렛지만 보낸다.
	UFUNCTION(NetMulticast, Reliable)
	void MulticastLadderTopOut(FVector LedgePoint, FVector LedgeNormal);

	// 꼭대기 위에 설 자리를 높이 구간 Band 에서 찾는다(서버, 등반, 사다리 공용).
	//   아무것도 무시하지 않는다 - 복구 뒤 되살아날 충돌로 서는 자리를 재야 한다.
	FVBTopOutResult FindTopOutStand(const FVector& Edge, const FVector& OutwardNormal, const FFloatInterval& Band) const;
	// 등반, 사다리 공용 꼭대기 올라서기. 매달림 올라서기 몽타주와 워프를 쓴다.
	void PlayTopOutMontage(const FVector& LedgePoint, const FVector& LedgeNormal, const TCHAR* From);

	// FrontLedge / BackLedge / BackFloor 모션 워핑 타겟을 갱신한다.
	void UpdateWarpTargets(const FVBTraversalCheckResult& Result, UAnimMontage* Montage);

	// 블렌드아웃 시작 콜백. 중단이면 즉시 복구한다(완료까지 기다리면 올라온 뒤 멈칫한다).
	void OnTraversalMontageBlendingOut(UAnimMontage* Montage, bool bInterrupted);

	// 몽타주 종료 콜백. 자연 종료만 여기서 복구한다.
	UFUNCTION()
	void OnTraversalMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	// 공통 복구. 세션이 임시로 바꾼 것은 여기서만 복구한다.
	void RecoverFromTraversal();
	// Falling 으로 복구한다. 모드의 놓기가 같이 쓴다.
	void RecoverByFalling();

	// 캡슐 스윕 헬퍼.
	bool CapsuleSweep(const FVector& Start, const FVector& End, FHitResult& OutHit) const;

	// 렛지에서 바깥으로 비켜 아래로 캡슐 스윕해 바닥을 찾는다.
	//   비키는 이유: 얇은 벽을 캡슐이 감싸면 벽 측면을 바닥으로 오인한다.
	bool TraceFloorBelowLedge(const FVector& LedgePoint, const FVector& LedgeNormal, float DownDepth, FHitResult& OutFloorHit) const;

private:
	friend struct FVBTraversalTestAccess;
	// 매달림, 등반은 호스트 구현이 하나뿐이라 인터페이스 없이 이 컴포넌트의 RPC 와 세션 전이를 직접 부른다.
	friend struct FVBHangMode;
	friend struct FVBClimbMode;

	AVBCharacter* GetCharacter() const;
	const UVBClimbAnimSet* GetClips() const { return ClimbAnimSet; }
	// 서버가 아닌 소유 클라이언트면 true. 축 입력 RPC 는 여기서만 보낸다(프록시가 남의 입력을 보내지 않게).
	bool IsOwningClient() const;
	// 자동 진입 의도가 한 번 꺼질 때까지 다시 붙지 않는다.
	void RequireFreshAutoIntent() { Reentry.bAutoNeedsFreshIntent = true; }
	// 설정한 시간 동안 공중 잡기가 렛지를 잡지 않게 한다.
	void BlockHangRegrab();
	// 권한에 따라 멀티캐스트나 서버 RPC 로 등반 시작, 놓기를 보낸다.
	void SendClimbStart(const FVector& SurfacePoint, const FVector& SurfaceNormal, UPrimitiveComponent* Surface);
	void SendClimbDrop();

	// 전방 렛지 트랜스폼을 플레이어 애님에 전달한다. 회전은 MakeRotFromZ(법선)이다(워프 축과 다르다).
	static void HandLedgeToAnim(UAnimInstance* Anim, const FVBTraversalCheckResult& Result);

	// 전방 렛지 스탠드 위치까지 캡슐이 갈 수 있나. 막히면 R.bHasFrontLedge 를 끄고 false.
	bool CheckRoomToFrontLedge(FVBTraversalCheckResult& R, const FVector& ActorLoc, float HalfH) const;
	// 상단 sweep 으로 깊이를 재고, 가로지를 공간이 없으면 뒤 렛지를 무효로 한다.
	void MeasureObstacleDepth(FVBTraversalCheckResult& R, float HalfH) const;
	// 뒤 렛지 아래 바닥을 찾아 R 에 적는다.
	void FindBackFloor(FVBTraversalCheckResult& R, float HalfH) const;

	// 공중 모드에서 이 렛지를 지금 잡아도 되나.
	bool PassesAirMantleGate(const FVBTraversalCheckResult& Result) const;
	// 동작이 정해진 뒤의 절차. 몽타주를 골라 서버에 요청하고, 공중에서 바닥이 없으면 매달림으로 간다.
	bool StartChosenTraversal(FVBTraversalCheckResult& Result, bool bAirMantleOnly);
	// chooser 후보를 MotionMatch 로 좁혀 몽타주를 고른다. 못 고르면 경고를 남기고 nullptr.
	UAnimMontage* SelectTraversalMontage(FVBTraversalChooserParams& Params, UAnimInstance* Anim, FPoseSearchBlueprintResult& OutMatch);

	UPROPERTY()
	TWeakObjectPtr<AVBCharacter> OwnerCharacter;

	friend struct FVBLadderIntentTickFunction;

	FVBLadderIntentTickFunction LadderIntentTick;

	// IVBLadderModeHost 구현. UHT 가 UCLASS 의 private 상속을 받지 않아 안쪽 클래스로 둔다.
	class FLadderHost final : public IVBLadderModeHost
	{
	public:
		explicit FLadderHost(UVBTraversalComponent& InOwner) : Owner(InOwner) {}
		virtual AVBCharacter* GetCharacter() const override;
		virtual const UVBClimbAnimSet* GetClips() const override;
		virtual const UVBTraversalConfig* GetConfig() const override;
		virtual void SetAttached(bool bAttached) override;
		virtual void EndSessionFalling() override;
		virtual void RequireFreshAutoIntent() override;
		virtual bool IsRidingLadder() const override;
		virtual bool IsInSurfaceMode() const override;
		virtual bool IsInAnyTraversal() const override;
		virtual bool IsBodyBusyForTraversal() const override;
		virtual void SendLadderStartRequest(AVBLadder* Ladder, float Height) override;
		virtual void SendLadderMoveInput(float InputY) override;
		virtual void SendLadderDropRequest() override;
		virtual void ConfirmLadderDrop() override;
		virtual void ConfirmLadderTopOut(const FVector& LedgePoint, const FVector& LedgeNormal) override;
		virtual FVBTopOutResult FindTopOutStand(const FVector& Edge, const FVector& OutwardNormal,
			const FFloatInterval& Band) const override;
		virtual void PlayTopOutMontage(const FVector& LedgePoint, const FVector& LedgeNormal, const TCHAR* From) override;
		virtual FVBLadderTickTargets GetLadderTickTargets() override;
	private:
		UVBTraversalComponent& Owner;
	};
	FLadderHost LadderHost{*this};

	// 다른 동작이 캐릭터를 점유하고 있어 트래버설을 시작하면 안 되는가. 자동 진입과 사다리 서버 재검사가 같이 쓴다.
	bool IsBodyBusyForTraversal() const;

	// 세션 단계와 붙은 면. SetTraversalState 만 쓰고 상태 태그와 틱도 거기서 맞춘다.
	EVBTraversalPhase TraversalPhase = EVBTraversalPhase::None;
	EVBTraversalSurface AttachedSurface = EVBTraversalSurface::None;
	bool bWarnedNoTagTarget = false; // ASC 가 없다는 경고는 한 번
	void SetTraversalState(EVBTraversalPhase NewPhase, EVBTraversalSurface NewSurface);
	void BeginMove();
	void Attach(EVBTraversalSurface NewSurface);
	void Detach(); // 붙어 있을 때만 Exiting 으로, 세션이 끝났으면 그대로
	void EndSession();

	// 세션이 캐릭터에서 빌린 것. 장애물 참조가 GC 에 보이도록 UPROPERTY 로 둔다.
	UPROPERTY()
	FVBTraversalSession Session;

	// 모드마다 자기 실행 상태를 가진다. 몽타주 참조가 GC 에 보이도록 UPROPERTY 로 둔다.
	UPROPERTY()
	FVBHangMode HangMode;
	UPROPERTY()
	FVBLadderMode LadderMode;
	UPROPERTY()
	FVBClimbMode ClimbMode;

	// 재진입 기억. 모드가 끝나도 남는다.
	struct FVBTraversalReentryMemory
	{
		// 웅크리기로 놓으면 켜지고, 자동 진입 의도가 한 번 꺼져야 풀린다(같은 자리를 다시 물지 않게).
		bool bAutoNeedsFreshIntent = false;
		// 다음 자동 판정 시각.
		double AutoTraversalNextCheckTime = 0.0;
		// 놓은 뒤 이 시각까지 공중 잡기가 렛지를 안 문다.
		double HangRegrabBlockUntil = 0.0;
	};
	FVBTraversalReentryMemory Reentry;

	FOnMontageEnded MontageEndedDelegate;
	FOnMontageBlendingOutStarted MontageBlendingOutDelegate;
};
