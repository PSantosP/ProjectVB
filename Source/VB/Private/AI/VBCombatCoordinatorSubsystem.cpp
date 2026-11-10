// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBCombatRingRules.h"
#include "AI/VBCombatTokenRules.h"

#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"
#include "TimerManager.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystemComponent.h"        // CancelAbilities - 처형 게이트
#include "AbilitySystemGlobals.h"          // GetAbilitySystemComponentFromActor
#include "AbilitySystem/VBGameplayTags.h"  // Ability.Attack
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "Data/VBCombatSettings.h"
#include "Data/VBEnemyConfig.h"
#include "VB/VB.h"


UVBCombatCoordinatorSubsystem* UVBCombatCoordinatorSubsystem::Get(const UObject* WorldContextObject)
{
	if (!GEngine)
	{
		return nullptr;
	}
	// 조율은 per-world -> world 서브시스템을 그 world 에서 직접 얻는다(GI 아님).
	if (UWorld* World = GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::LogAndReturnNull))
	{
		return World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	}

	return nullptr;
}

void UVBCombatCoordinatorSubsystem::Deinitialize()
{
	// 그룹이 사라지면 타이머도 사라진다 - 월드 소멸 시 명시적으로 해제한다.
	if (UWorld* World = GetWorld())
	{
		for (TPair<TWeakObjectPtr<AActor>, FVBCombatGroup>& Pair : Groups)
		{
			World->GetTimerManager().ClearTimer(Pair.Value.RecomputeTimer);
		}
	}

	Groups.Empty();
	Super::Deinitialize();
}

bool UVBCombatCoordinatorSubsystem::IsServer() const
{
	const UWorld* World = GetWorld();
	// 월드가 없으면 권위가 아닌 것으로 본다.
	return World && World->GetNetMode() != NM_Client;
}

void UVBCombatCoordinatorSubsystem::RegisterCombatant(UVBEnemyCombatComponent* Member, AActor* Target)
{
	if (!IsServer() || !Member || !Target)
	{
		return;
	}

	FVBCombatGroup& Group = Groups.FindOrAdd(Target);
	Group.Target = Target;
	// 이미 있으면 기존 결정을 그대로 둔다(멱등).
	Group.Members.FindOrAdd(Member);
	EnsureRecomputeTimer(Group, Target);
}

void UVBCombatCoordinatorSubsystem::UnregisterCombatant(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	// 기록이 사라지면 그 HeldTokenCost 도 빠져 가용량이 자동 회복된다.
	const TWeakObjectPtr<AActor> TargetKey = Member->CombatTarget;
	FVBCombatGroup* Group = Groups.Find(TargetKey);
	if (!Group)
	{
		return;
	}

	Group->Members.Remove(Member);
	if (Group->Members.Num() == 0)
	{
		DisbandGroup(TargetKey);
	}
}

bool UVBCombatCoordinatorSubsystem::IsEngaged(const AActor* Target) const
{
	if (!Target)
	{
		return false;
	}
	// 약참조 키로 조회만 한다.
	const FVBCombatGroup* Group = Groups.Find(TWeakObjectPtr<AActor>(const_cast<AActor*>(Target)));
	return Group && Group->Members.Num() > 0;
}

bool UVBCombatCoordinatorSubsystem::TryAcquireAttackToken(UVBEnemyCombatComponent* Member, int32 Cost)
{
	if (!IsServer() || !Member)
	{
		return false;
	}

	FVBCombatGroup* Group = FindGroupForMemberMutable(Member);
	if (!Group)
	{
		return false;
	}

	FVBCombatMemberState* State = Group->Members.Find(Member);
	if (!State || State->HeldTokenCost > 0)
	{
		// 이미 보유하고 있으면 중복 취득하지 않는다.
		return false;
	}

	// ClampMin 은 에디터만 막으므로 런타임 바닥을 잡는다.
	const int32 EffectiveCost = VBCombatTokenRules::EffectiveCost(Cost);
	if (ComputeAvailableTokens(*Group) < EffectiveCost)
	{
		return false;
	}

	// 새 임대 - 지난 임대의 흔적을 먼저 지운다(남으면 '이미 공격 중'으로 오판).
	State->ClearLease();

	State->HeldTokenCost = EffectiveCost;
	State->TokenGrantedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;

	// 위치 기준 회수의 기준점. 세울 수 없으면 -1 이고 그때는 위치로 회수하지 않는다.
	const AActor* GrantTarget = Group->Target.Get();
	const AActor* GrantOwner = Member->GetOwner();
	State->DistanceAtGrant = (GrantTarget && GrantOwner)
		? FVector::Dist2D(GrantOwner->GetActorLocation(), GrantTarget->GetActorLocation())
		: -1.0;

	// 공격을 먼저 고른다. 이후 접근 거리, 역할 문턱은 고른 공격의 시작 거리를 본다.
	if (GrantTarget)
	{
		Member->PlanNextAttack(GrantTarget->GetActorLocation());
	}

	VB_LOG(Log, "토큰 부여: %s (코스트 %d, 잔여 %d)",
		*GetNameSafe(Member->GetOwner()), EffectiveCost, ComputeAvailableTokens(*Group));
	return true;
}

void UVBCombatCoordinatorSubsystem::NotifyAttackStarted(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	FVBCombatGroup* Group = FindGroupForMemberMutable(Member);
	FVBCombatMemberState* State = Group ? Group->Members.Find(Member) : nullptr;
	if (!State)
	{
		return;
	}

	// 토큰 없이 공격이 시작되면 결함이다(동시 공격 상한이 조용히 무너진다).
	VB_CLOG(State->HeldTokenCost <= 0, Warning,
		"토큰 없이 공격이 시작됐다: %s - 상한 계산이 이미 어긋나 있다",
		*GetNameSafe(Member->GetOwner()));

	State->AttackStartedTime = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
}

void UVBCombatCoordinatorSubsystem::ReleaseAttackToken(UVBEnemyCombatComponent* Member)
{
	if (!IsServer() || !Member)
	{
		return;
	}

	FVBCombatGroup* Group = FindGroupForMemberMutable(Member);
	FVBCombatMemberState* State = Group ? Group->Members.Find(Member) : nullptr;
	if (!State)
	{
		return;
	}

	// 계획은 토큰과 수명이 같다. 다음 부여가 그때 거리로 다시 잰다.
	Member->ClearPlannedAttack();

	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	VB_LOG(Log, "토큰 반납: %s (공격 %.2fs)", *GetNameSafe(Member->GetOwner()),
		State->AttackStartedTime >= 0.0 ? Now - State->AttackStartedTime : 0.0);
	// 숨: 아키타입이 정한 만큼 다음 부여를 미룬다. 편차는 서버 RNG.
	if (const UVBEnemyConfig* PauseConfig = Member->GetCombatConfig())
	{
		if (PauseConfig->PostAttackPauseSeconds > 0.0f)
		{
			const float Pause = VBCombatTokenRules::PostAttackPause(PauseConfig->PostAttackPauseSeconds,
				FMath::RandRange(-PauseConfig->PostAttackPauseVariance, PauseConfig->PostAttackPauseVariance));
			Group->NextGrantAllowedTime = FMath::Max(Group->NextGrantAllowedTime, Now + Pause);
			VB_LOG(Log, "숨: 다음 공격자는 %.2f초 뒤", Pause);
		}
	}

	State->ClearLease();
	// 방금 때린 멤버는 순번 규칙이 위치로 자연히 후순위로 보낸다.

	// 다음 적이 바로 붙도록 그 자리에서 재배분한다.
	// 처형 취소의 하강 에지로 재진입할 수 있다 - 임대를 먼저 비웠고 멤버 목록은 바꾸지 않는다.
	RecomputeGroup(*Group);
}

FVector UVBCombatCoordinatorSubsystem::GetAssignedSlotLocation(const UVBEnemyCombatComponent* Member) const
{
	if (const FVBCombatMemberState* State = FindMemberState(Member))
	{
		return State->SlotLocation;
	}
	// 미배정이면 자기 위치 - 가짜 슬롯을 지어내지 않고 제자리 대기가 된다.
	const AActor* Owner = Member ? Member->GetOwner() : nullptr;
	return Owner ? Owner->GetActorLocation() : FVector::ZeroVector;
}

EVBCombatRole UVBCombatCoordinatorSubsystem::GetAssignedRole(const UVBEnemyCombatComponent* Member) const
{
	if (const FVBCombatMemberState* State = FindMemberState(Member))
	{
		return State->Role;
	}

	return EVBCombatRole::Idle;
}

int32 UVBCombatCoordinatorSubsystem::GetHeldTokenCost(const UVBEnemyCombatComponent* Member) const
{
	const FVBCombatMemberState* State = FindMemberState(Member);
	return State ? State->HeldTokenCost : 0;
}

int32 UVBCombatCoordinatorSubsystem::ComputeAvailableTokens(const FVBCombatGroup& Group) const
{
	int32 Available = GetDefault<UVBCombatSettings>()->MaxAttackTokens;
	for (const TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		Available -= Pair.Value.HeldTokenCost;
	}

	return Available;
}

const FVBCombatGroup* UVBCombatCoordinatorSubsystem::FindGroupForMember(const UVBEnemyCombatComponent* Member) const
{
	if (!Member)
	{
		return nullptr;
	}
	// 약참조 원본을 키로 쓴다 - Get() 은 타겟 파괴 후 null 이라 그룹을 못 찾는다.
	return Groups.Find(Member->CombatTarget);
}

FVBCombatGroup* UVBCombatCoordinatorSubsystem::FindGroupForMemberMutable(const UVBEnemyCombatComponent* Member)
{
	return const_cast<FVBCombatGroup*>(FindGroupForMember(Member));
}

const FVBCombatMemberState* UVBCombatCoordinatorSubsystem::FindMemberState(const UVBEnemyCombatComponent* Member) const
{
	const FVBCombatGroup* Group = FindGroupForMember(Member);
	if (!Group)
	{
		return nullptr;
	}
	// 맵 키 타입 때문에 const 를 벗긴다. 읽기 전용 조회다.
	const TWeakObjectPtr<UVBEnemyCombatComponent> MemberKey(const_cast<UVBEnemyCombatComponent*>(Member));
	return Group->Members.Find(MemberKey);
}

void UVBCombatCoordinatorSubsystem::EnsureRecomputeTimer(FVBCombatGroup& Group, AActor* Target)
{
	if (Group.RecomputeTimer.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World || !Target)
	{
		return;
	}

	// 0 이하 주기는 SetTimer 가 타이머 삭제로 읽는다. ini 수기 편집에 대비해 바닥을 잡는다.
	const float Interval = FMath::Max(GetDefault<UVBCombatSettings>()->SlotRecomputeInterval, KINDA_SMALL_NUMBER);

	const TWeakObjectPtr<AActor> TargetKey(Target);
	World->GetTimerManager().SetTimer(
		Group.RecomputeTimer,
		FTimerDelegate::CreateUObject(this, &UVBCombatCoordinatorSubsystem::OnRecomputeTimer, TargetKey),
		Interval,
		/*bLoop=*/true);
}

void UVBCombatCoordinatorSubsystem::DisbandGroup(const TWeakObjectPtr<AActor>& TargetKey)
{
	if (FVBCombatGroup* Group = Groups.Find(TargetKey))
	{
		// 제거보다 먼저 - 구조체가 파괴되면 타이머가 고아가 된다.
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(Group->RecomputeTimer);
		}

		Groups.Remove(TargetKey);
	}
}

void UVBCombatCoordinatorSubsystem::OnRecomputeTimer(TWeakObjectPtr<AActor> TargetKey)
{
	if (!IsServer())
	{
		return;
	}

	FVBCombatGroup* Group = Groups.Find(TargetKey);
	if (!Group)
	{
		return;
	}

	// 타겟이 없거나 죽었으면 해체한다. 멤버는 다음 BTService 주기에 스스로 재등록한다.
	AActor* Target = Group->Target.Get();
	if (!Target || UVBAbilitySystemStatics::IsActorDead(Target))
	{
		DisbandGroup(TargetKey);
		return;
	}

	RecomputeGroup(*Group);

	// 멤버가 전부 빠졌으면 해체한다. RecomputeGroup 은 Groups 를 안 건드려 포인터가 유효하다.
	if (Group->Members.Num() == 0)
	{
		DisbandGroup(TargetKey);
	}
}

void UVBCombatCoordinatorSubsystem::RevokeApproachLease(
	const TWeakObjectPtr<UVBEnemyCombatComponent>& MemberKey, FVBCombatMemberState& State,
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& Deprioritized)
{
	State.ClearLease();
	// 다른 후보 뒤에 붙여 남는 토큰만 다시 받게 한다.
	if (MemberKey->IsReadyToAttack())
	{
		Deprioritized.Add(MemberKey);
	}
}

void UVBCombatCoordinatorSubsystem::RecomputeGroup(FVBCombatGroup& Group)
{
	AActor* Target = Group.Target.Get();
	if (!Target)
	{
		return;
	}
	// 대상이 처형 중이면 손을 뗀다. 해산하지 않아 끝나면 그대로 이어 싸운다.
	if (UVBAbilitySystemStatics::IsActorFinishing(Target))
	{
		HaltForFinisher(Group);
		return;
	}

	PruneMembers(Group);
	if (Group.Members.Num() == 0)
	{
		return;
	}

	const FVector TargetLocation = Target->GetActorLocation();
	OfferTokens(Group, TargetLocation);
	AssignRoles(Group, TargetLocation);
	AssignRingSlots(Group);
}

void UVBCombatCoordinatorSubsystem::HaltForFinisher(FVBCombatGroup& Group)
{
	for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		UVBEnemyCombatComponent* Member = Pair.Key.Get();
		AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}
		// 예고 중이든 스윙 중이든 끊는다. 예고만 끊으면 이미 뽑은 칼이 연출 위로 내려온다.
		if (UAbilitySystemComponent* MemberASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(MemberOwner))
		{
			FGameplayTagContainer AttackTags;
			AttackTags.AddTag(VBGameplayTags::Ability_Attack);
			MemberASC->CancelAbilities(&AttackTags);
		}
		// 토큰을 회수한다. 남으면 처형 뒤 배분이 막힌다.
		if (Pair.Value.HeldTokenCost > 0)
		{
			ReleaseAttackToken(Member);
		}
	}
}

void UVBCombatCoordinatorSubsystem::PruneMembers(FVBCombatGroup& Group)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	for (auto It = Group.Members.CreateIterator(); It; ++It)
	{
		const UVBEnemyCombatComponent* Member = It.Key().Get();
		AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner || UVBAbilitySystemStatics::IsActorDead(MemberOwner))
		{
			It.RemoveCurrent();
			continue;
		}
		// 공격이 시작된 임대만 본다. 접근 중인 임대는 OfferTokens 가 맡는다.
		FVBCombatMemberState& State = It.Value();
		if (State.HeldTokenCost <= 0 || State.AttackStartedTime < 0.0 || (Now - State.AttackStartedTime) <= Settings->MaxTokenHoldSeconds)
		{
			continue;
		}
		// 공격 중인가는 구독 실패를 잡기 위해 기록이 아닌 ASC 태그에 묻는다.
		if (Member->IsAttacking())
		{
			// 스윙 중에 거두면 동시 공격 상한이 무너지므로 유지한다.
			VB_CLOG(!State.bReportedHoldOverrun, Warning,
				"공격이 %.1fs 째 끝나지 않는다: %s - 임대를 유지해 동시 공격 상한을 지킨다",
				Now - State.AttackStartedTime, *MemberOwner->GetName());
			State.bReportedHoldOverrun = true;
			continue;
		}

		VB_LOG(Warning, "토큰 임대 만료: %s (공격 %.1fs) - 정상 반납 경로가 실패했다",
			*MemberOwner->GetName(), Now - State.AttackStartedTime);
		State.ClearLease();
	}
}

void UVBCombatCoordinatorSubsystem::GatherTokenFacts(const FVBCombatGroup& Group, const FVector& TargetLocation, double Now,
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& OutKeys, TArray<FVBTokenMember>& OutFacts) const
{
	OutKeys.Reset(Group.Members.Num());
	OutFacts.Reset(Group.Members.Num());
	for (const TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}

		const FVBCombatMemberState& State = Pair.Value;
		const UVBEnemyConfig* Config = Member->GetCombatConfig();
		FVBTokenMember& Facts = OutFacts.AddDefaulted_GetRef();
		Facts.Distance = FVector::Dist2D(MemberOwner->GetActorLocation(), TargetLocation);
		Facts.PlannedStart = Member->GetAttackStartDistance();
		Facts.PlannedRelease = Member->GetAttackReleaseDistance();
		Facts.MaxApproachHoldSeconds = Config->MaxApproachHoldSeconds;
		Facts.SecondsSinceGrant = Now - State.TokenGrantedTime;
		Facts.DistanceAtGrant = State.DistanceAtGrant;
		Facts.bHeld = State.HeldTokenCost > 0;
		Facts.bAttackStarted = State.AttackStartedTime >= 0.0;
		Facts.bWantsCounter = Member->WantsArmoredCounter();
		OutKeys.Add(Pair.Key);
	}
}

void UVBCombatCoordinatorSubsystem::OfferTokens(FVBCombatGroup& Group, const FVector& TargetLocation)
{
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Keys;
	TArray<FVBTokenMember> Facts;
	GatherTokenFacts(Group, TargetLocation, Now, Keys, Facts);

	// 후보 = 토큰이 없고 지금 공격을 켤 수 있는 멤버.
	TArray<int32> Candidates;
	for (int32 Index = 0; Index < Keys.Num(); ++Index)
	{
		if (!Facts[Index].bHeld && Keys[Index]->IsReadyToAttack())
		{
			Candidates.Add(Index);
		}
	}

	// 접근 중인 임대를 거둔다(재배분은 결함이 아니라 Log). 거둔 멤버는 준비됐을 때만 대기열 뒤에 선다.
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> DeprioritizedKeys;
	for (const VBCombatTokenRules::FRevocation& Revocation : VBCombatTokenRules::SelectRevocations(Facts, Candidates))
	{
		const TWeakObjectPtr<UVBEnemyCombatComponent>& Key = Keys[Revocation.MemberIndex];
		const FVBTokenMember& Member = Facts[Revocation.MemberIndex];
		const FString Name = GetNameSafe(Key->GetOwner());
		switch (Revocation.Reason)
		{
		case VBCombatTokenRules::ERevokeReason::ApproachOverrun:
			VB_LOG(Log, "토큰 재배분: %s - %.1fs 동안 사거리에 못 붙어 대기열 뒤로 보낸다", *Name, Member.SecondsSinceGrant);
			break;
		case VBCombatTokenRules::ERevokeReason::MovedAway:
			VB_LOG(Log, "토큰 이동: %s 가 %.0f 로 멀어졌다(부여 시점 %.0f) - 사거리 안 후보에게 넘긴다", *Name, Member.Distance, Member.DistanceAtGrant);
			break;
		case VBCombatTokenRules::ERevokeReason::ArmoredCounter:
			VB_LOG(Log, "토큰 재배분: %s - 슈퍼아머 반격 요청자에게 넘긴다(접근 중이던 임대 회수)", *Name);
			break;
		}

		RevokeApproachLease(Key, Group.Members.FindChecked(Key), DeprioritizedKeys);
	}

	TArray<int32> Deprioritized;
	for (const TWeakObjectPtr<UVBEnemyCombatComponent>& Key : DeprioritizedKeys)
	{
		Deprioritized.Add(Keys.IndexOfByKey(Key));
	}

	// 예고가 겹치지 않게, 예고 중인 멤버가 있으면 이번 주기는 주지 않는다.
	for (const TWeakObjectPtr<UVBEnemyCombatComponent>& Key : Keys)
	{
		if (Key->IsTelegraphing())
		{
			return;
		}
	}
	// 반격 요청은 임대에 따라 달라질 수 있다 - 거둔 뒤의 사실로 순서를 정한다.
	GatherTokenFacts(Group, TargetLocation, Now, Keys, Facts);
	const bool bInPause = Now < Group.NextGrantAllowedTime;
	// 부여는 TryAcquireAttackToken 하나뿐. 반격 요청자는 숨을 기다리지 않는다.
	for (int32 Index : VBCombatTokenRules::OrderOffers(Facts, Candidates, Deprioritized))
	{
		UVBEnemyCombatComponent* Member = Keys[Index].Get();
		if (!Member || (bInPause && !Member->WantsArmoredCounter()))
		{
			continue;
		}

		TryAcquireAttackToken(Member, Member->GetCombatConfig()->AttackTokenCost);
	}
}

void UVBCombatCoordinatorSubsystem::AssignRoles(FVBCombatGroup& Group, const FVector& TargetLocation)
{
	for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}

		FVBCombatMemberState& State = Pair.Value;
		const EVBCombatRole PreviousRole = State.Role;
		// Z 를 무시한다 - 계단, 경사의 높이차가 거리를 튀게 해 역할이 흔들린다.
		const double Distance = FVector::Dist2D(MemberOwner->GetActorLocation(), TargetLocation);
		State.Role = VBCombatTokenRules::NextRole(State.HeldTokenCost > 0, PreviousRole, Distance,
			Member->GetAttackStartDistance(), Member->GetAttackReleaseDistance());
		// 전환 에지만 남긴다 - 매 주기 찍으면 전환 순간이 묻힌다.
		VB_CLOG(State.Role != PreviousRole, Log, "역할 전환: %s %s -> %s (거리 %.0f)",
			*MemberOwner->GetName(),
			*UEnum::GetValueAsString(PreviousRole), *UEnum::GetValueAsString(State.Role), Distance);
	}
}

void UVBCombatCoordinatorSubsystem::AssignRingSlots(FVBCombatGroup& Group)
{
	const UVBCombatSettings* Settings = GetDefault<UVBCombatSettings>();
	AActor* Target = Group.Target.Get();
	if (!Target)
	{
		return;
	}

	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Waiting;
	TArray<FVBCombatRingMember> Inputs;
	GatherRingMembers(Group, Waiting, Inputs);
	if (Waiting.Num() == 0)
	{
		return;
	}

	const FVector Center = Target->GetActorLocation();
	const TArray<FVBCombatRingAssignment> Assignments = VBCombatRingRules::AssignSlots(Center, Settings->SurroundSlotCount, Inputs);

	// 내비 투영은 배정이 끝난 목표점에만 건다(멤버당 1회, 짝짓기는 순수 기하로).
	UNavigationSystemV1* NavSystem = UNavigationSystemV1::GetCurrent(GetWorld());
	const FVector ProjectionExtent(Settings->SlotNavProjectionExtent);

	for (const FVBCombatRingAssignment& Assignment : Assignments)
	{
		if (FVBCombatMemberState* State = Group.Members.Find(Waiting[Assignment.MemberIndex]))
		{
			FVector SlotLocation = Assignment.Location;
			if (NavSystem)
			{
				// 투영 실패 시 원래 좌표를 그대로 쓴다.
				FNavLocation Projected;
				if (NavSystem->ProjectPointToNavigation(SlotLocation, Projected, ProjectionExtent))
				{
					SlotLocation = Projected.Location;
				}
			}

			const int32 PreviousSlot = State->SlotIndex;
			State->SlotIndex = Assignment.SlotIndex;
			State->SlotLocation = SlotLocation;

			// 배정이 바뀔 때만 찍는다. 슬롯 위치와 멤버 거리로 미배정과 이동 실패를 가른다.
			const AActor* MemberOwner = Waiting[Assignment.MemberIndex].IsValid()
				? Waiting[Assignment.MemberIndex]->GetOwner()
				: nullptr;
			VB_CLOG(PreviousSlot != Assignment.SlotIndex && MemberOwner, Log,
				"슬롯 배정: %s idx=%d 타겟에서 %.0f 멤버까지 %.0f",
				*GetNameSafe(MemberOwner), Assignment.SlotIndex,
				FVector::Dist2D(State->SlotLocation, Center),
				FVector::Dist2D(State->SlotLocation, MemberOwner ? MemberOwner->GetActorLocation() : Center));
		}
	}

	// 슬롯보다 대기자가 많으면 남는 멤버는 슬롯 없이 제자리 대기한다(배정 로그가 없는 것이 단서).
}

void UVBCombatCoordinatorSubsystem::GatherRingMembers(FVBCombatGroup& Group, TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& OutWaiting,
	TArray<FVBCombatRingMember>& OutInputs)
{
	OutWaiting.Reserve(Group.Members.Num());
	OutInputs.Reserve(Group.Members.Num());

	// 모두 슬롯 없음 + 자기 위치로 되돌린 뒤 대기자만 모은다(낡은 슬롯이 남지 않게).
	for (TPair<TWeakObjectPtr<UVBEnemyCombatComponent>, FVBCombatMemberState>& Pair : Group.Members)
	{
		const UVBEnemyCombatComponent* Member = Pair.Key.Get();
		const AActor* MemberOwner = Member ? Member->GetOwner() : nullptr;
		if (!MemberOwner)
		{
			continue;
		}

		Pair.Value.SlotIndex = INDEX_NONE;
		Pair.Value.SlotLocation = MemberOwner->GetActorLocation();

		if (Pair.Value.Role == EVBCombatRole::Surrounder)
		{
			const UVBEnemyConfig* MemberConfig = Member->GetCombatConfig();
			FVBCombatRingMember Input;
			Input.Location = MemberOwner->GetActorLocation();
			Input.WaitingDistance = MemberConfig
				? VBCombatRingRules::WaitingDistance(MemberConfig->AttackCommitDistance, MemberConfig->WaitingStandoff)
				: 0.0;
			OutWaiting.Add(Pair.Key);
			OutInputs.Add(Input);
		}
	}
}
