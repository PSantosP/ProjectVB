// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/EngineTypes.h" // FTimerHandle
#include "Subsystems/WorldSubsystem.h"
#include "AI/VBCombatTypes.h"
#include "VBCombatCoordinatorSubsystem.generated.h"

class UVBEnemyCombatComponent;
struct FVBCombatRingMember;
struct FVBTokenMember;

/**
 * 그룹 안 멤버 하나에 대한 코디네이터의 결정 기록(역할, 토큰, 슬롯).
 * UPROPERTY 없음 - 약참조와 POD 뿐이라 GC 등록이 필요 없다.
 */
USTRUCT()
struct FVBCombatMemberState
{
	GENERATED_BODY()

	EVBCombatRole Role = EVBCombatRole::Idle;

	// 보유한 토큰 코스트. 0 = 미보유. 합이 곧 사용 중인 토큰량이다
	int32 HeldTokenCost = 0;

	int32 SlotIndex = INDEX_NONE;
	FVector SlotLocation = FVector::ZeroVector;

	// 임대 시작 시각(= 접근 시작). MaxApproachHoldSeconds 가 이 값으로 잰다
	double TokenGrantedTime = 0.0;

	// 공격 GA 가 켜진 시각. -1 = 아직 접근 중(0 은 유효한 게임시간이다)
	double AttackStartedTime = -1.0;

	// 토큰을 받은 순간 타겟까지 수평 거리(cm). -1 = 기준 없음. 위치 기준 회수가 "부여 때보다 멀어졌나" 를 묻는 데 쓴다
	double DistanceAtGrant = -1.0;

	// 이번 임대에서 공격 구간 초과를 이미 보고했는가(경고 반복 방지)
	bool bReportedHoldOverrun = false;

	// 임대 필드를 미보유 상태로 되돌린다. Role/Slot 은 건드리지 않는다. 모든 회수 지점이 이것을 쓴다
	void ClearLease()
	{
		HeldTokenCost = 0;
		AttackStartedTime = -1.0;
		DistanceAtGrant = -1.0;
		bReportedHoldOverrun = false;
	}
};

/** 한 타겟을 둘러싼 전투 그룹 하나. 위와 같은 이유로 UPROPERTY 없음. */
USTRUCT()
struct FVBCombatGroup
{
	GENERATED_BODY()

	TWeakObjectPtr<AActor> Target;

	TMap<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState> Members;

	// 그룹당 1개. Tick 대신 이 타이머가 역할과 슬롯을 재계산한다
	FTimerHandle RecomputeTimer;
	// 다음 토큰을 줄 수 있는 월드 시각. 토큰 해제 때 PostAttackPauseSeconds 로 설정한다
	double NextGrantAllowedTime = 0.0;
};

// 월드의 타겟별 전투 그룹과 공격 토큰 원장을 소유하고, 값 함수의 결과를 원장에 적용한다(서버 전용, 복제 없음, 그룹당 타이머 1개).
UCLASS()
class VB_API UVBCombatCoordinatorSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	UFUNCTION(BlueprintPure, Category="VB|AI", meta=(WorldContext="WorldContextObject"))
	static UVBCombatCoordinatorSubsystem* Get(const UObject* WorldContextObject);

	//~ Begin USubsystem
	virtual void Deinitialize() override;
	//~ End USubsystem

	// 멤버를 타겟 그룹에 넣는다. 멱등 - 기존 결정을 보존한다. 그룹이 없으면 만들고 타이머를 건다.
	void RegisterCombatant(UVBEnemyCombatComponent* Member, AActor* Target);

	// 멤버 기록을 지운다. 보유 토큰도 함께 사라져 가용량이 회복된다.
	void UnregisterCombatant(UVBEnemyCombatComponent* Member);

	// 이 대상을 노리는 적이 하나라도 있나(서버 전용, 클라에서는 늘 false)
	bool IsEngaged(const AActor* Target) const;

	// 지정 코스트로 토큰을 즉시 취득 시도. 정상 경로는 주기 배분이고 이것은 외부용 진입점이다.
	bool TryAcquireAttackToken(UVBEnemyCombatComponent* Member, int32 Cost);

	// 토큰 해제(Attacking 하강 에지). 그 자리에서 RecomputeGroup 을 돌므로 그룹 순회 도중에 부르면 안 된다
	void ReleaseAttackToken(UVBEnemyCombatComponent* Member);

	// 공격 개시 통지(Attacking 상승 에지). 임대 시계를 접근이 아닌 공격 시작부터 다시 찍는다.
	void NotifyAttackStarted(UVBEnemyCombatComponent* Member);

	// 배정된 슬롯 위치. 미배정이면 자기 위치다
	FVector GetAssignedSlotLocation(const UVBEnemyCombatComponent* Member) const;
	// 배정된 역할. 미등록이면 Idle 이다
	EVBCombatRole GetAssignedRole(const UVBEnemyCombatComponent* Member) const;

	// 이 멤버가 보유한 토큰 코스트. 0 = 미보유. 역할은 토큰에서 파생되므로 역할로 역산하지 않는다
	int32 GetHeldTokenCost(const UVBEnemyCombatComponent* Member) const;

private:
#if WITH_DEV_AUTOMATION_TESTS
	friend class FVBCombatLeaseReproAccess;
	friend class FVBCombatAuditTestAccess;
#endif
	// 타이머 콜백. TMap 재해싱으로 주소가 바뀌므로 그룹 참조가 아닌 타겟 키를 나른다
	void OnRecomputeTimer(TWeakObjectPtr<AActor> TargetKey);

	// 처형 멈춤 또는 정리 -> 토큰 배분 -> 역할 배정 -> 슬롯 배정. 그룹 상태를 바꾸는 유일한 함수다
	void RecomputeGroup(FVBCombatGroup& Group);
	// 대상이 처형을 거는 동안 멤버의 공격을 끊고 토큰을 거둔다
	void HaltForFinisher(FVBCombatGroup& Group);
	// 무효, 사망 멤버를 빼고 공격이 끝났는데 해제되지 않은 임대를 회수한다(공격 중이면 유지)
	void PruneMembers(FVBCombatGroup& Group);
	// 멤버마다 토큰 규칙(VBCombatTokenRules)이 읽는 사실. 키와 사실은 같은 차례(그룹 맵 순서)다
	void GatherTokenFacts(const FVBCombatGroup& Group, const FVector& TargetLocation, double Now,
		TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& OutKeys, TArray<FVBTokenMember>& OutFacts) const;
	// 후보 모으기 -> 접근 중 임대 거두기 -> 예고, 숨 게이트 -> 차례대로 부여
	void OfferTokens(FVBCombatGroup& Group, const FVector& TargetLocation);
	// 토큰과 거리로 역할을 정한다(히스테리시스). 전환만 로그
	void AssignRoles(FVBCombatGroup& Group, const FVector& TargetLocation);
	// 접근 중 임대를 비우고, 준비된 멤버는 대기열 뒤에 세운다
	static void RevokeApproachLease(const TWeakObjectPtr<UVBEnemyCombatComponent>& MemberKey,
		FVBCombatMemberState& State, TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& Deprioritized);

	// 수집 -> VBCombatRingRules -> NavMesh 투영 -> 장부. RecomputeGroup 의 마지막 단계다
	void AssignRingSlots(FVBCombatGroup& Group);
	// 모든 멤버의 슬롯을 비우고, 대기자와 그 입력을 같은 차례로 모은다
	void GatherRingMembers(FVBCombatGroup& Group, TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& OutWaiting,
		TArray<FVBCombatRingMember>& OutInputs);

	// 그룹 해체. 타이머를 먼저 해제하고 맵에서 지운다
	void DisbandGroup(const TWeakObjectPtr<AActor>& TargetKey);

	// 그룹에 재계산 타이머가 없으면 건다(멱등)
	void EnsureRecomputeTimer(FVBCombatGroup& Group, AActor* Target);

	// 멤버의 소속 그룹. 멤버의 CombatTarget 이 곧 그룹 키라 역인덱스를 두지 않는다
	const FVBCombatGroup* FindGroupForMember(const UVBEnemyCombatComponent* Member) const;
	FVBCombatGroup* FindGroupForMemberMutable(const UVBEnemyCombatComponent* Member);

	const FVBCombatMemberState* FindMemberState(const UVBEnemyCombatComponent* Member) const;

	// 잔여 토큰 = 총량 - 멤버 보유량의 합. 저장하지 않고 매번 계산한다
	int32 ComputeAvailableTokens(const FVBCombatGroup& Group) const;

	bool IsServer() const;

	// 타겟별 전투 그룹. 토큰 원장의 유일한 저장소다
	TMap<TWeakObjectPtr<AActor>, FVBCombatGroup> Groups;
};
