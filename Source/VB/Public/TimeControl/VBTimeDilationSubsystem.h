// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "Subsystems/WorldSubsystem.h"
#include "VBTimeDilationSubsystem.generated.h"

/** 활성 dilation 요청 하나(배율 + 만료 타이머). */
USTRUCT()
struct FVBTimeDilationRequest
{
	GENERATED_BODY()

	// 시간 배율(1.0 = 정상, 0.01 = 거의 정지)
	float Scale = 1.0f;

	// timed 요청의 만료 타이머. 무기한 요청이면 무효 핸들.
	FTimerHandle ExpiryTimer;
};

/** 순서 요청(PushSequence)의 한 단계 - 배율과 실초 길이. 단계를 밟아 정상으로 돌아오는 연출용. */
USTRUCT(BlueprintType)
struct FVBTimeDilationStep
{
	GENERATED_BODY()

	// 시간 배율(1.0 = 정상, 0.01 = 거의 정지)
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|TimeControl", meta=(ClampMin="0.01", ClampMax="1.0"))
	float Scale = 0.5f;

	// 실초 길이. 0 이하인 단계는 건너뛴다(무기한 Push 로 새지 않게).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|TimeControl", meta=(ClampMin="0.0"))
	float RealDuration = 0.1f;
};

/**
 * 전역, 액터 시간 dilation 의 단일 소유자. 요청을 Handle 로 쌓고 가장 느린 것이 이긴다(min-wins).
 * SetGlobalTimeDilation 을 직접 부르면 다른 요청과 서로 덮어쓰므로 반드시 여기를 거친다.
 * 네트워크 월드에서는 적용하지 않는다(복제되지 않는 값이라).
 */
UCLASS()
class VB_API UVBTimeDilationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="VB|TimeControl", meta=(WorldContext="WorldContextObject"))
	static UVBTimeDilationSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;
	//~ End USubsystem

	// 실시간(초) 동안만 거는 전역 요청. 같은 Handle 재요청은 교체한다.
	UFUNCTION(BlueprintCallable, Category="VB|TimeControl")
	void PushTimed(FName Handle, float Scale, float RealDuration);

	// 무기한 전역 요청. Pop 까지 유지
	UFUNCTION(BlueprintCallable, Category="VB|TimeControl")
	void Push(FName Handle, float Scale);

	// Handle 요청과 남은 순서 단계를 버린다. 없으면 무동작.
	UFUNCTION(BlueprintCallable, Category="VB|TimeControl")
	void Pop(FName Handle);

	// 단계를 차례로 건다. 같은 Handle 재요청은 처음부터 다시, 마지막 단계가 끝나면 Handle 이 빠진다.
	void PushSequence(FName Handle, const TArray<FVBTimeDilationStep>& Steps);

	// 지금 적용 중인 전역 배율(시험, 진단용)
	float GetAppliedScale() const { return AppliedScale; }

	// 액터 단위 timed 요청. 전역과 같은 규칙으로 CustomTimeDilation 에 쓰고, 다 빠지면 1.0 으로 되돌린다.
	UFUNCTION(BlueprintCallable, Category="VB|TimeControl")
	void PushActorTimed(AActor* Actor, FName Handle, float Scale, float RealDuration);

private:
	// 시험이 타이머를 기다리지 않고 만료를 일으킨다.
	friend class FVBTimeDilationTestAccess;

	TMap<FName, FVBTimeDilationRequest> ActiveRequests;

	// 마지막으로 실제 적용한 배율. 바뀔 때만 다시 쓴다.
	float AppliedScale = 1.0f;

	// 활성 요청의 min(없으면 1.0)을 계산해, 변했을 때만 적용.
	void Resolve();

	// Handle 만료 콜백(타이머). 남은 순서 단계가 있으면 다음 단계를 걸고, 없으면 요청 제거 후 Resolve.
	void OnRequestExpired(FName Handle);

	// PushTimed 의 본체. 남은 순서 단계를 건드리지 않는다(순서 진행용).
	void PushTimedStep(FName Handle, float Scale, float RealDuration);

	// Handle 별 남은 순서 단계. 지금 걸린 단계는 ActiveRequests 에 있다.
	TMap<FName, TArray<FVBTimeDilationStep>> PendingSteps;

	// Handle 의 기존 만료 타이머를 clear(있으면). 교체/제거 전 호출.
	void ClearExistingTimer(FName Handle);

	TMap<TWeakObjectPtr<AActor>, TMap<FName, FVBTimeDilationRequest>> ActorRequests;
	// 액터 하나의 min 을 계산해 CustomTimeDilation 에 쓴다(없으면 1.0)
	void ResolveActor(const TWeakObjectPtr<AActor>& Actor);
	void OnActorRequestExpired(TWeakObjectPtr<AActor> Actor, FName Handle);

	// 요청을 실제 시간에 적용해도 되는가. 네트워크 월드는 거짓(기록, 만료는 그대로 돈다).
	bool CanChangeTime();
	bool bReportedNetworkSkip = false;
};
