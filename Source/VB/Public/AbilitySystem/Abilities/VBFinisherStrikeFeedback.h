// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Character/VBMeleeTraceComponent.h" // FVBMeleeTraceHits - 판정 창 콜백을 값으로 받는다
#include "GameplayTagContainer.h"

class AActor;
class UAbilitySystemComponent;
class UVBFinisherConfig;
class UVBSwingCombatProfile;
struct FGameplayEventData;
struct FHitResult;

// 타격 연출이 지금 알아야 할 사실. 대상이 언제 비는지가 동작이라 능력이 호출마다 새로 만들어 넘긴다.
struct FVBFinisherStrikeContext
{
	const UObject* Ability = nullptr;
	const AActor* Avatar = nullptr;
	AActor* Target = nullptr;
	UAbilitySystemComponent* OwnerASC = nullptr;
	const UVBFinisherConfig* Config = nullptr;
	// 처형 설정과 모양 표가 큐를 정하지 않았을 때의 타격 큐.
	FGameplayTag FallbackCue;
	// 창이 끝날 때까지 안 닿았어도 이 거리(cm) 안이면 가장 가까운 점에 낸다. 0 이면 끈다.
	float NearMissReach = 0.0f;
	bool bSceneActive = false;
};

// 처형 장면의 타격 연출. 공격자 몽타주의 판정 창마다 대상에 처음 닿은 순간 피 큐와 화면 큐를 한 번 낸다. 피해는 없다.
// 창 열림과 닫힘 이벤트 구독은 능력이 맡고, 이 구조체는 창 하나의 상태와 큐를 맡는다.
struct VB_API FVBFinisherStrikeFeedback
{
	// 처음 부르면 true. 능력은 이때 창 이벤트를 구독한다.
	bool TryBeginListening();
	bool IsListening() const { return bListening; }

	// 판정 창을 연다. MakeHitCallback 은 슬롯 번호로 이 창의 접촉 콜백을 만든다.
	void OpenStrike(const FGameplayEventData& Payload, const FVBFinisherStrikeContext& Context,
		TFunctionRef<FVBMeleeTraceHits(int32)> MakeHitCallback);

	// 노티의 끝 알림. 몽타주가 끊겨 닫힌 창(EventMagnitude 0)은 보완도 경고도 하지 않는다.
	void CloseStrikes(const FGameplayEventData& Payload, const FVBFinisherStrikeContext& Context);

	// 판정 창의 접촉 보고. 대상에 처음 닿은 보고만 쓴다.
	void ReportHits(const TArray<FHitResult>& Hits, int32 Slot, const FVBFinisherStrikeContext& Context);

	// 장면이 끝나거나 끊겼다. 열린 창을 닫고 비운다.
	void End(const FVBFinisherStrikeContext& Context);

	// 날 위 샘플 중 대상까지 가장 가까운 점을 찾는다. DistanceTo 가 음수면 그 점을 못 잰 것이다.
	static bool FindClosestBladePoint(const FVector& Base, const FVector& Tip, TFunctionRef<float(const FVector&)> DistanceTo,
		float& OutDistance, FVector& OutPoint);

private:
	// 노티 하나 = 타격 하나. 같은 모양 창이 겹쳐 열리면 앞의 것을 닫고 연다.
	struct FStrike
	{
		int32 WindowId = INDEX_NONE;
		FGameplayTag Shape;
		FGameplayTag CueTag;
		TWeakObjectPtr<const UVBSwingCombatProfile> CombatProfile;
		bool bFired = false;
		// 창이 열릴 때의 날 가운데.
		FVector BeginBladeCenter = FVector::ZeroVector;
	};

	void CloseStrike(int32 Slot, bool bReachedEnd, const FVBFinisherStrikeContext& Context);
	void FireStrike(int32 Slot, const FHitResult& Hit, const FVBFinisherStrikeContext& Context);

	TArray<FStrike> Strikes;
	bool bListening = false;
};
