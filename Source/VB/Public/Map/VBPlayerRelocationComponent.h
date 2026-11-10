// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "VBPlayerRelocationComponent.generated.h"

class APawn;

DECLARE_MULTICAST_DELEGATE(FVBOnRelocationStarted);

/**
 * 서버에서 플레이어를 어두운 화면 뒤로 옮긴다: 어둡게 -> 복원 컴포넌트로 순간 이동 -> 밝게. 빠른 이동과 스크립트 이동이 같이 쓴다.
 * 목록과 이동 판정은 UVBFastTravelComponent 가 맡는다. AVBPlayerController 에 붙는다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBPlayerRelocationComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBPlayerRelocationComponent();

	// 어두워지기 시작한 때부터 밝히기를 걸 때까지 true.
	bool IsRelocating() const { return bRelocating; }

	// 서버에서만. 판정을 통과한 빠른 이동을 시작한다. 어두워진 뒤 목적지, 폰, 전투를 다시 보고 옮긴다.
	void StartPointTravel(const FGameplayTag& Destination, float InFadeOutSeconds, float InFadeInSeconds);

	// 서버에서만. 화면을 어둡게 하는 동안 폰을 붙들었다가 Arrival 로 옮기고 밝힌다. 지금 옮길 수 없으면 아무것도 안 하고 false.
	// 도착하면 OnArrivedEffects 를 한 번 적용하고, 실패하면 붙든 자리로 돌아간다.
	bool StartScriptedMove(const FTransform& Arrival, float InFadeOutSeconds, float InFadeInSeconds,
		const TArray<FInstancedStruct>& OnArrivedEffects);

	// 서버에서만. 도착 알림을 더 기다리지 않고 지금 옮기기를 끝낸다. 지키미 타이머가 부른다.
	void AbortRelocation();

	// 옮기기를 받아들인 직후, 어두워지기 전에 알린다.
	FVBOnRelocationStarted OnRelocationStarted;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 도착 알림을 기다리는 상한 = 복원 컴포넌트의 스트리밍 대기 상한 + 이 여유(초). 넘으면 이동을 끝낸다.
	UPROPERTY(EditDefaultsOnly, Category="VB|FastTravel", meta=(ClampMin="0"))
	float WatchdogSlackSeconds = 2.0f;

private:
	// 소유 클라이언트: 화면을 어둡게(bOut) 또는 밝게. 카메라 매니저 페이드.
	UFUNCTION(Client, Reliable)
	void ClientTravelFade(bool bOut, float Seconds);

	// 서버: 어두워진 뒤 옮긴다.
	void MovePawnToDestination();

	// 서버: 복원 컴포넌트에 순간 이동 하나를 맡긴다. 결과 알림을 시작 전에 묶고 작업 ID 를 저장한다. 시작하지 못하면 false.
	bool BeginRelocation(APawn* Pawn, UVBPlayerRestoreComponent* Restore, const FVBTeleportRequest& Request, bool bScripted,
		float InFadeInSeconds);

	// 복원 컴포넌트의 순간 이동 결과. 지금 맡긴 작업 ID 가 아니면 버린다.
	void HandleTeleportFinished(FGuid OperationId, EVBPlayerRestoreOutcome Outcome);

	// 맡긴 작업을 놓고(결과 묶음, 지키미 해제) 밝힌다.
	void FinishTravel(float InFadeInSeconds);
	void ReleaseRelocation();
	APawn* GetPawn() const;

	// 지금 맡긴 순간 이동 하나. 끝나면 통째로 비운다. 폰은 맡긴 그 폰이다.
	struct FRelocation
	{
		bool bScripted = false;
		FGuid OperationId;
		TWeakObjectPtr<APawn> Pawn;
		TWeakObjectPtr<UVBPlayerRestoreComponent> Restore;
		float FadeInSeconds = 0.0f;
	};

	FRelocation Relocation;
	FDelegateHandle TeleportFinishedHandle;
	FTimerHandle FadeTimer;
	FTimerHandle WatchdogTimer;
	FGameplayTag PendingDestination;
	float PendingFadeInSeconds = 0.0f;
	bool bRelocating = false;

	// 스크립트 이동의 도착 효과 사본. 안의 객체 참조가 옮기는 동안 살아 있게 UPROPERTY 로 둔다.
	UPROPERTY(Transient)
	TArray<FInstancedStruct> PendingArrivedEffects;
};
