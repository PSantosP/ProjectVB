// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Camera/CameraModifier.h"
#include "Curves/CurveFloat.h"
#include "Engine/EngineTypes.h"
#include "Tickable.h"
#include "VBCombatFeedbackModifier.generated.h"

class APawn;
class UMaterialInterface;
class UMaterialInstanceDynamic;
class UNiagaraComponent;

/**
 * 화면 충격 한 번의 저작 값. 시간은 전부 실초다(슬로우 중에도 화면은 제 속도로 끝난다).
 * 홈 = UVBCombatFeedbackProfile::Screen. 패링, 가드, 강타가 같은 구조체를 자산 값만 달리해 쓴다.
 */
USTRUCT(BlueprintType)
struct FVBScreenImpactSettings
{
	GENERATED_BODY()

	// 진행 중인 화면 충격보다 낮으면 새 사건을 버린다. 같거나 높으면 처음부터 다시 건다.
	//   약한 가드가 방금 시작한 패링의 화면을 끊지 않게 하는 값이다. 월드 FX 는 이 값과 무관하게 난다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	int32 Priority = 0;

	// 회전, FOV, 방향 반동이 사는 시간.
	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="0.0", Units="s"))
	float CameraSeconds = 0.24f;

	// 화면 재질의 유지 시간. 전달받은 월드 FX 는 이 시간까지 실시간 기준으로 돌고, 그 뒤는 일반 시계로 자기 수명을 마친다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="0.0", Units="s"))
	float EffectSeconds = 0.5f;

	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="0.0", Units="Hz"))
	float Frequency = 18.0f;

	// 떨림 진폭(도). 0 이면 떨지 않는다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	FRotator RotationAmplitude = FRotator(1.4f, 0.65f, 0.8f);

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	FVector PhaseRadians = FVector(0.0f, 1.57f, 0.78f);

	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	float FOVAmplitude = 0.65f;

	// 사건 방향을 화면에 투영해 그쪽으로 한 번 미는 각도(도). 진동 없이 감쇠만 한다. 방향 없는 사건에는 안 쓴다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(Units="deg"))
	float KickDegrees = 0.0f;

	// 사건 방향의 화면 안쪽 성분을 시야각으로 보이는 크기(도). 찌르기처럼 반동이 안 보이는 타를 보인다. 0 = 끔.
	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(Units="deg"))
	float KickForwardFOV = 0.0f;

	// 화면 재질에 전달하는 세기(재질 파라미터 ParryAmount). 이름은 첫 사용처의 것이고 뜻은 충격 세기다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="0.0"))
	float EdgeStrength = 0.8f;

	UPROPERTY(EditAnywhere, Category="VB|Feedback", meta=(ClampMin="1.0"))
	float DecayExponent = 3.0f;

	// 후처리 재질(M_VB_ParryEdge 계열 MI). 계약: 스칼라 ParryAmount(세기), ParryAge(실초 나이). 비면 화면 재질 없이 카메라만.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	TObjectPtr<UMaterialInterface> Material;

	// 화면 재질 세기의 시간 모양(가로 0..1 = EffectSeconds, 세로 = EdgeStrength 배율). 비우면 DecayExponent 감쇠.
	//   채우면 0 에서 올라 0 으로 끝나야 한다 - 아니면 첫 프레임이 번쩍이거나 끝이 끊긴다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	FRuntimeFloatCurve MaterialEnvelope;

	// 폰과 감독이 등록한 처형 카메라 사이를 오가도 끊지 않는다. 끄면 뷰가 바뀌는 즉시 끝난다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	bool bFollowFinisherHandoff = false;

	// 같은 프로필, 같은 우선순위의 연타면 진행 중인 화면, 슬로우를 그대로 둔다. 끄면 처음부터 다시 건다.
	UPROPERTY(EditAnywhere, Category="VB|Feedback")
	bool bKeepPhaseOnRetrigger = false;
};

/**
 * 로컬 카메라의 전투 화면 충격 소비자 하나(패링, 가드, 강타 공용).
 * 상태는 여기만 든다 - 큐 CDO, Character, 시간 서브시스템에는 화면 상태를 두지 않는다.
 * 한 번에 사건 하나: 새 사건은 Priority 규칙으로 교체되거나 버려진다.
 */
UCLASS()
class VB_API UVBCombatFeedbackModifier : public UCameraModifier, public FTickableGameObject
{
	GENERATED_BODY()

public:
	UVBCombatFeedbackModifier();

	// 사건 하나를 시작한다(WorldDirection = 방향 반동, Effects = 실초로 돌릴 Niagara, SourceProfile = 같은 사건 판정). 거절이면 false, Effects 는 그대로.
	bool StartFeedback(APawn* Pawn, const FVBScreenImpactSettings& Settings, const FVector& WorldDirection,
		const TArray<UNiagaraComponent*>& Effects, const UObject* SourceProfile = nullptr);

	// MaterialEnvelope 가 쓸 만한지(비어 있으면 통과). 프로필 저장 검사와 StartFeedback 이 같이 쓴다.
	static bool ValidateMaterialEnvelope(const FVBScreenImpactSettings& Settings, FString& OutReason);

	// 지금 화면 재질 인스턴스(읽기 전용).
	const UMaterialInstanceDynamic* GetScreenMaterialInstance() const { return EdgeInstance; }

	// 방향 반동 한 프레임의 크기(감쇠 전). 오른쪽 = 요 +, 위쪽 = 피치 +, 안쪽 = 시야각 -.
	struct FDirectionalKick { float Yaw = 0.0f; float Pitch = 0.0f; float FOV = 0.0f; };
	static FDirectionalKick ComputeDirectionalKick(const FVector& WorldDirection, const FRotator& ViewRotation, float KickDegrees,
		float KickForwardFOV);

	// 사건 하나의 세기 배율을 화면 값에 곱한다(떨림, 시야각, 반동). 무기별 스윙 무게(UVBAttackConfig::SwingShakeScale)가 이리로 온다.
	static FVBScreenImpactSettings ScaleSettings(const FVBScreenImpactSettings& Settings, float Scale);
	virtual bool ModifyCamera(float DeltaTime, FMinimalViewInfo& InOutPOV) override;
	void StopFeedback();
	bool IsFeedbackActive() const { return bActive; }
	virtual void Tick(float DeltaTime) override;
	virtual bool IsTickable() const override;
	virtual bool IsTickableWhenPaused() const override { return true; }
	virtual UWorld* GetTickableGameObjectWorld() const override { return GetWorld(); }
	virtual TStatId GetStatId() const override;
	virtual void BeginDestroy() override;

private:
	UFUNCTION()
	void HandleCameraEndPlay(AActor* Actor, EEndPlayReason::Type Reason);
	static void ReleaseEffect(UNiagaraComponent* Effect);
	bool IsAllowedViewTarget(const APawn* Pawn) const;
	// 진행 중인 사건이 이 나이에 아직 살아 있나(Tick, ModifyCamera, 교체 규칙 공용).
	bool IsStillRunning(double Age) const;
	float EvaluateMaterialAmount(float Age) const;

	// 진행 중인 사건의 설정 사본 - 재질, 외부 곡선이 사건 동안 GC 에 지워지지 않게 UPROPERTY 로 든다.
	UPROPERTY(Transient)
	FVBScreenImpactSettings ActiveSettings;
	TWeakObjectPtr<const UObject> ActiveSource;
	FVector ActiveDirection = FVector::ZeroVector;
	TWeakObjectPtr<APawn> ActivePawn;
	TWeakObjectPtr<AActor> ActiveViewTarget;
	TArray<TWeakObjectPtr<UNiagaraComponent>> ActiveEffects;
	UPROPERTY()
	TObjectPtr<UMaterialInstanceDynamic> EdgeInstance;
	UPROPERTY()
	TObjectPtr<UMaterialInterface> EdgeSource;
	double StartedAt = 0.0;
	bool bActive = false;
};
