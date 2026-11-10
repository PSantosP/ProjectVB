// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "Interaction/VBTimedMove.h"
#include "SaveSystem/VBSaveHold.h"
#include "VBElevator.generated.h"

class ACharacter;
class UStaticMeshComponent;
class UVBInteractableComponent;

/**
 * 두 층을 오가는 승강기. 상태는 Interactable 의 bActivated(위 층으로)이고, 쓰는 길은 저장 막기를 얻은 뒤의 RequestStop 하나다.
 * 발판 위치 = Move(복제) + 서버 시각. 이동 중에는 게임 저장을 막고, 막히는 폰이 있으면 그 자리에 멈춰 기다린다.
 * 움직이는 발판 위에서는 트래버설을 시작하지 않는다(IsBaseMoving).
 */
UCLASS()
class VB_API AVBElevator : public AActor
{
	GENERATED_BODY()

public:
	AVBElevator();

	// 서버에서만. 그 층으로 보낸다. 저장 막기를 못 얻었거나 승강로가 막혀 있으면 false(상태 그대로).
	bool RequestStop(bool bUp);

	// 발판이 멈춰 서 있지 않은가(오가는 중이거나 막혀서 기다리는 중).
	bool IsMoving() const;

	// 그 캐릭터가 서 있는 바닥이 움직이는 승강기 발판인가. 트래버설 시작 검사가 쓴다.
	static bool IsBaseMoving(const ACharacter* Character);

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Elevator")
	TObjectPtr<UStaticMeshComponent> Deck;

	// 발판의 조작판. 이 컴포넌트의 켜짐이 위 층이다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Elevator")
	TObjectPtr<UVBInteractableComponent> Interactable;

	// 아래 층(알파 0) 기준 위 층 발판의 상대 위치.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator")
	FVector UpperOffset = FVector(0.0, 0.0, 800.0);

	// 이동 속도(cm/s).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="1.0"))
	float Speed = 200.0f;

	// 막힘 판정에서 발판 상자를 줄이는 여유(cm). 층 바닥 구멍에 딱 맞는 발판이 가장자리에 닿아 막힘으로 읽히지 않게.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="0.0"))
	float SweepInset = 2.0f;

	// 승강로 검사(BeginPlay)에서 발판 위에 비워 두어야 할 높이(cm) - 서 있는 사람 키.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="0.0"))
	float StandingHeadroom = 180.0f;

	// 발판에 탄 사람을 찾는 높이(cm, 발판 윗면 위). 점프 중인 사람도 들어올 만큼.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="0.0"))
	float RiderSearchHeight = 250.0f;

	// 막혀 멈춘 동안 다시 갈 수 있나 볼 때 앞으로 재 보는 거리(cm)와 그 간격(초).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="1.0"))
	float ResumeProbeDistance = 8.0f;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Elevator", meta=(ClampMin="0.0"))
	float ResumeProbeInterval = 0.2f;

private:
	UFUNCTION()
	void HandleActivationChanged(bool bActivated, bool bInstant);

	UFUNCTION()
	void HandleInteracted(AActor* InteractingActor);

	UFUNCTION()
	void OnRep_Move();

	UPROPERTY(ReplicatedUsing=OnRep_Move)
	FVBTimedMove Move;

	float CurrentAlpha() const;
	void PlaceDeck(float Alpha);
	// 서버: 알파 From -> To 한 걸음이 막혔나(다른 폰, 탄 사람의 머리 위).
	bool IsStepBlocked(float FromAlpha, float ToAlpha) const;
	// 서버, BeginPlay: 승강로 전체가 비었나(정적 지형). 막혔으면 이 승강기는 움직이지 않는다.
	bool IsShaftClear() const;
	void StartMove(float ToAlpha);
	// 움직이지 않고 그 알파에 둔다(시작, 세이브 복원, 움직일 수 없을 때).
	void SnapTo(float Alpha);
	void ArriveAndRest();
	bool AcquireSaveHold();
	void ReleaseSaveHold();

	FVBSaveHoldToken SaveHold;
	bool bShaftClear = true;
	float LastPlacedAlpha = 0.0f;
	double NextResumeProbeTime = 0.0;
};
