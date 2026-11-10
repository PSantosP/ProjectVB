// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "VBFinisherCamera.generated.h"

class UCameraComponent;
class UVBFinisherConfig;
struct FVBFinisherCameraCollisionSettings;

struct FVBFinisherCameraView
{
	FVector Location = FVector::ZeroVector;
	int32 Candidate = INDEX_NONE;
	bool bSafe = false;
	bool bParticipantsVisible = false;
};

/**
 * 처형 한 판을 비추는 카메라 액터(복제 안 함). 뷰타깃이 되어 설정의 카메라 키를 따라가며, 처형마다 하나 산다.
 * 슬로우에 같이 느려지지 않게 월드 시간이 아니라 씬 재생 시간을 쓴다. 뷰타깃 전환, 복귀는 서브시스템이 든다.
 */
UCLASS()
class VB_API AVBFinisherCamera : public AActor
{
	GENERATED_BODY()

public:
	AVBFinisherCamera();

	/** 스폰 직후 한 번: 카메라 키 기준 액터(InAnchor)와 씬 재생 시간을 읽을 참가자(InTimeSource)를 전달한다. */
	void Setup(const UVBFinisherConfig* InConfig, AActor* InAnchor, AActor* InTimeSource);

	virtual void Tick(float DeltaSeconds) override;
	bool HasSafeView() const { return bHasSafeView; }

	// 참가자와 소유 부속을 제외하고 충돌과 참가자 시야를 함께 검사한다.
	static FVBFinisherCameraView ResolveView(UWorld& World, const FVBFinisherCameraCollisionSettings& Settings,
		const FVector& Desired, const FVector& LookAt, TConstArrayView<AActor*> Participants, const FMatrix& Projection,
		int32 PreviousCandidate = INDEX_NONE);

	// 복귀할 게임플레이 카메라에 전달할 현재 시선이다.
	FRotator GetViewRotation() const;

private:
	// 씬 재생 시간을 초 단위로. 못 읽으면 음수를 돌려준다(장면이 안 돌거나 바인딩이 없는 경우).
	float GetSceneTime() const;
	FMatrix GetViewProjection(float FOV) const;

	UPROPERTY()
	TObjectPtr<UCameraComponent> CameraComponent;

	UPROPERTY()
	TObjectPtr<const UVBFinisherConfig> Config;

	// 약참조다. 처형은 대상을 죽일 수 있고, 죽은 액터를 강참조로 붙들면 그 시체가 안 사라진다.
	TWeakObjectPtr<AActor> Anchor;
	TWeakObjectPtr<AActor> TimeSource;
	TArray<TWeakObjectPtr<AActor>> Participants;
	int32 LastCandidate = INDEX_NONE;
	bool bWasUnresolved = false;
	bool bHasSafeView = false;

	// 앵커가 사라진 뒤에도 마지막 자리를 유지하기 위한 기록. 없으면 원점으로 튄다.
	FTransform LastGoodWorld = FTransform::Identity;
	bool bHasLastGood = false;
};
