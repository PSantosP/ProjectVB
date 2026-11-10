// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Narrative/Story/VBStoryStateBinding.h"
#include "Player/VBPlayerPreparationBinding.h"
#include "GameFramework/Actor.h"
#include "GameplayTagContainer.h"
#include "StructUtils/InstancedStruct.h"
#include "VBAreaTrigger.generated.h"

class APawn;
class APlayerController;
class APlayerState;
class UBoxComponent;
class UPrimitiveComponent;
class UVBPlayerReadinessComponent;
class UVBStoryStateComponent;

DECLARE_MULTICAST_DELEGATE(FVBOnAreaOccupancyChanged);

namespace VBAreaTrigger
{
	// 만족(안에 있음 + 조건 참)이 거짓 -> 참으로 바뀔 때만 발화한다. 한 번만 영역은 이미 발화했으면 안 한다.
	VB_API bool ShouldFire(bool bWasSatisfied, bool bNowSatisfied, bool bOnce, bool bOnceAlreadyFired);
	// 효과 중 하나라도 준비된 플레이어가 필요한가(FVBEffect::NeedsReadyPlayer).
	VB_API bool NeedsReadyPlayer(const TArray<FInstancedStruct>& Effects);
	// 효과 중 받아들인 뒤 나중에 끝나는 것(장면)이 몇 개인가(FVBEffect::ReportsDeferredResult).
	VB_API int32 CountDeferredEffects(const TArray<FInstancedStruct>& Effects);
}

/**
 * 영역 진입 - 플레이어가 상자 안에 있고 조건이 참이 되면 사실 -> 사건 -> 효과 순으로 적용한다. 서버 전용.
 * 겹침 신호에서 바로 판정하지 않고 다음 틱에 본다: 이어하기는 폰 생성 뒤에 저장 사실을 적용하기 때문이다.
 * 한 번만 영역은 OnceFact 로 기억한다(저장되므로 이어하기가 다시 걸지 않는다).
 */
UCLASS()
class VB_API AVBAreaTrigger : public AActor
{
	GENERATED_BODY()

public:
	AVBAreaTrigger();

	// 현재 물리 겹침만 읽는다. 준비, 조건, OnceFact, 지난 발화와 독립이며 저장하지 않는다.
	bool IsPawnInside(const APawn* Pawn) const;

	// 이 장면을 한 번만(OnceFact) 트는가 - 불러온 뒤에는 다시 틀지 않으므로 그 장면의 완료 영수증은 다시 생기지 않는다.
	bool PlaysSceneOnce(const UObject* Scene) const;

	// 겹침 입력이 바뀌었을 수 있다(서버). 구독자는 현재 폰으로 다시 조회하며 승인 bool을 캐시하지 않는다.
	FVBOnAreaOccupancyChanged OnOccupancyChanged;

#if WITH_EDITOR
	virtual EDataValidationResult IsDataValid(class FDataValidationContext& Context) const override;
#endif

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// 영역. 크기와 회전은 배치에서 정한다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Story")
	TObjectPtr<UBoxComponent> Area;

	// 전부 참이어야 발화한다. 비면 들어오기만 하면 된다. 안에 있는 동안 참이 되어도 발화한다(사실이 바뀌면 다시 본다).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story",
		meta=(BaseStruct="/Script/VB.VBCondition", ExcludeBaseStruct))
	TArray<FInstancedStruct> Conditions;

	// 발화할 때 적용한다(사건 뒤).
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(BaseStruct="/Script/VB.VBEffect", ExcludeBaseStruct))
	TArray<FInstancedStruct> Effects;

	// 사건(Event.Area.Entered)의 대상 태그. 퀘스트 목표가 여기에 질의를 건다. 비면 사건을 내지 않는다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	FGameplayTagContainer EventTags;

	// 적으면 한 번만 발화한다(이 사실이 켜져 있으면 이미 발화함). 비우면 새로 만족할 때마다 발화한다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story", meta=(Categories="Story"))
	FGameplayTag OnceFact;

	// 일회 영역만 사용한다. 준비 전에는 edge/OnceFact/사건/효과를 소진하지 않고, 준비 통지 뒤 현재 점유를 다시 본다.
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category="VB|Story")
	bool bRequirePlayerPreparation = false;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBEncounterConsumerTestAccess;
#endif
	UFUNCTION()
	void HandleBeginOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex, bool bFromSweep, const FHitResult& SweepResult);

	UFUNCTION()
	void HandleEndOverlap(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor, UPrimitiveComponent* OtherComp,
		int32 OtherBodyIndex);

	// 다음 틱에 한 번 판정한다. 같은 틱의 신호 여럿은 하나로 합친다.
	void RequestEvaluate();
	void Evaluate();
	bool IsSatisfiedFor(APawn& Pawn);
	bool TryFire(APawn& Pawn);
	// 장면이 닫혔다 - 성공(완료, 건너뛰기 + 완료 효과까지)이면 OnceFact 를 쓰고, 아니면 아무것도 쓰지 않는다(다음 진입에 다시).
	void HandleSceneClosed(bool bCompleted, TWeakObjectPtr<APlayerState> Player);
	void RefreshPlayerBindings();
	void DisconnectPlayerBindings();

	// 플레이어 폰마다 지난 판정의 만족. 거짓 -> 참 일 때만 발화한다.
	TMap<TWeakObjectPtr<APawn>, bool> LastSatisfied;

	FVBStoryStateBinding StoryBinding;
	bool bEvaluatePending = false;
	bool bWarnedNoStory = false;
	bool bWarnedMissingOnce = false;
	FVBPlayerPreparationBinding PlayerBinding;
	bool bEnding = false;
	// 장면을 요청해 결과를 기다리는 중(실행 중에만 - 저장하지 않는다). 그동안 다시 발화하지 않는다.
	bool bScenePending = false;
};
