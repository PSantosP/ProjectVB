// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Animation/VBAnimationTypes.h"
#include "Character/VBWalkReasons.h"
#include "Components/ActorComponent.h"
#include "VBLocomotionIntentComponent.generated.h"

// 서버가 정하는 이동 의도. 원격 클라가 걸음과 질주를 같은 RepNotify 에서 함께 받도록 한 속성으로 복제한다.
USTRUCT()
struct FVBReplicatedLocomotionIntent
{
	GENERATED_BODY()

	// 걷기 또는 달리기만 담는다. 질주는 bSprinting 이 따로 이기며 애님은 질주면 Sprint, 아니면 Gait 로 읽는다
	UPROPERTY()
	EVBGait Gait = EVBGait::Run;

	UPROPERTY()
	bool bSprinting = false;
};

/**
 * 캐릭터의 이동 의도(걸음, 질주, 조준, 스트레이프)를 소유한다. 걸음, 질주, 스트레이프는 서버가, 조준은 소유 클라가 예측해 쓴다.
 * 속도 계산과 회전 모드 적용은 이동 컴포넌트가 매 틱 여기서 읽어 한다.
 */
UCLASS(ClassGroup=(VB))
class VB_API UVBLocomotionIntentComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBLocomotionIntentComponent();
	static UVBLocomotionIntentComponent* FindFor(const AActor* Actor);
	// 이 컴포넌트가 없는 액터(적 등)는 질주하지 않는다
	static bool IsSprintingFor(const AActor* Actor);
	// 이 컴포넌트가 없는 액터(적 등)는 조준하지 않는다
	static bool IsAimingFor(const AActor* Actor);

	UFUNCTION(BlueprintPure, Category="VB|Locomotion")
	EVBGait GetGait() const { return Intent.Gait; }
	UFUNCTION(BlueprintPure, Category="VB|Locomotion")
	bool GetIsSprinting() const { return Intent.bSprinting; }
	UFUNCTION(BlueprintPure, Category="VB|Camera")
	bool WantsToAim() const { return bWantsToAim; }
	bool WantsToStrafe() const { return bWantsToStrafe; }

	// 소유 클라 또는 서버. 서버에서 질주를 켠다
	void StartSprint();
	// 소유 클라 또는 서버. 서버에서 질주를 끈다
	void StopSprint();

	// 소유 클라 또는 서버. 래그돌, 죽음, 처형 중에는 켜지지 않고 켜면 질주를 멈춘다. 소유 클라가 먼저 쓰고 서버 복제 값이 덮는다
	UFUNCTION(BlueprintCallable, Category="VB|Camera")
	void SetWantsToAim(bool bAim);
	// 서버. 스트레이프를 바꾼다(지금은 스마트 오브젝트 입구만 부른다)
	void SetWantsToStrafe(bool bStrafe);

	// 서버. 스마트 오브젝트 상호작용에 들어갈 때 질주, 웅크림, 조준, 스트레이프를 풀고 걸음을 덮어쓴다
	void EnterSmartObjectInputState(bool bWalk);
	// 서버. 나올 때 저장해 둔 스트레이프, 조준으로 되돌리고 걸음 덮어쓰기를 푼다. 걸음은 되쓰지 않아 그 사이에 바뀐 토글, 아날로그 걷기가 남는다
	void RestoreSmartObjectInputState(bool bPreviousStrafe, bool bPreviousAim);

	// 소유 클라의 입력. 걷기 토글은 서버가 뒤집는다(클라가 복제된 Gait 의 반대를 보내면 아날로그 걷기 중의 Walk 를 토글로 오해한다)
	void ToggleWalk();
	// 소유 클라의 입력. 스틱 크기(데드존 뒤)가 문턱을 넘나들 때만 서버에 알린다. 키보드는 1 이라 걷지 않는다
	void ReportMoveInputMagnitude(float StickMagnitude, float WalkRunThreshold);
	// 빙의가 바뀔 때(UnPossess 뒤 Possess) 보고 캐시와 서버의 아날로그 걷기 사유를 해제한다. 원격 클라는 캐시만 해제하며 서버 사유는 UnPossess 가 이미 해제했다
	void ResetAnalogWalk();

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

private:
	// 서버. 질주를 켜면 걷기 토글이 풀리고 질주가 끝나도 걷기로 돌아가지 않는다. 아날로그 걷기는 남는다
	void SetSprinting(bool bNewSprinting);
	// 서버. 걷는 이유로 Gait 를 다시 정한다. Gait 를 쓰는 유일한 곳이다
	void RefreshGait();
	// 조준을 막는 조건(래그돌, 죽음, 처형)은 래그돌 컴포넌트와 ASC 에 묻는다
	bool CanAim() const;

	UFUNCTION(Server, Reliable)
	void ServerSetSprinting(bool bNewSprinting);
	UFUNCTION(Server, Reliable)
	void ServerSetWantsToAim(bool bAim);
	UFUNCTION(Server, Reliable)
	void ServerToggleWalk();
	UFUNCTION(Server, Reliable)
	void ServerSetAnalogWalk(bool bAnalogWalk);

	// 소유 클라도 받는다(COND_None). 걸음과 질주는 서버만 정한다
	UPROPERTY(Replicated)
	FVBReplicatedLocomotionIntent Intent;

	// 걸음, 질주 구조체 밖인 이유: 소유 클라가 먼저 쓰는 값이라 서버 확정 구조체와 덮이는 때가 다르다
	UPROPERTY(Replicated)
	bool bWantsToAim = false;

	// 기본 true = 캡슐이 카메라를 따른다. 실제 회전 모드는 이동 컴포넌트가 정한다
	UPROPERTY(EditAnywhere, Replicated, Category="VB|Locomotion")
	bool bWantsToStrafe = true;

	// 서버가 드는 걷는 이유들. 복제하지 않고 결과인 Gait 만 복제된다
	FVBWalkReasons WalkReasons;
	// 소유 클라가 마지막으로 알린 아날로그 걷기. 같은 값이면 RPC 를 보내지 않는다(서버의 시작값 false 와 같게 시작)
	bool bAnalogWalkReported = false;
};
