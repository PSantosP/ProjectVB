// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "VBCombatAuditTestAbility.h"
#include "VBTestAbilityTags.h"
#include "AI/VBAIController.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "Data/VBCombatSettings.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

UVBCombatAuditTestAbility::UVBCombatAuditTestAbility()
{
	InputTag = VBGameplayTags::Input_Attack_Light;
	SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility));
}

bool UVBCombatAuditTestAbility::CanActivateAbility(const FGameplayAbilitySpecHandle Handle,
	const FGameplayAbilityActorInfo* ActorInfo, const FGameplayTagContainer* SourceTags,
	const FGameplayTagContainer* TargetTags, FGameplayTagContainer* OptionalRelevantTags) const
{
	const AActor* Avatar = ActorInfo ? ActorInfo->AvatarActor.Get() : nullptr;
	const UVBEnemyCombatComponent* Combat = Avatar ? Avatar->FindComponentByClass<UVBEnemyCombatComponent>() : nullptr;
	return Combat && !Combat->HasAttackToken();
}

UVBBossRowTestAbility::UVBBossRowTestAbility()
{
	InputTag = VBGameplayTags::Input_Attack_Armored;
	SetAssetTags(FGameplayTagContainer(VBTestAbilityTags::AnyAbility));
}

#if WITH_DEV_AUTOMATION_TESTS
class FVBCombatAuditTestAccess
{
public:
	static FVBCombatGroup* Group(UVBCombatCoordinatorSubsystem& Coordinator, UVBEnemyCombatComponent* Member)
	{
		return Coordinator.FindGroupForMemberMutable(Member);
	}

	static void Revoke(const TWeakObjectPtr<UVBEnemyCombatComponent>& MemberKey,
		FVBCombatMemberState& State, TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& Queue)
	{
		UVBCombatCoordinatorSubsystem::RevokeApproachLease(MemberKey, State, Queue);
	}

	static void OriginalRevoke(const TWeakObjectPtr<UVBEnemyCombatComponent>& MemberKey,
		FVBCombatMemberState& State, TArray<TWeakObjectPtr<UVBEnemyCombatComponent>>& Queue)
	{
		State.ClearLease();
		if (MemberKey->IsReadyToAttack())
		{
			Queue.Add(MemberKey);
		}
	}

	static void Recompute(UVBCombatCoordinatorSubsystem& Coordinator, UVBEnemyCombatComponent* Member)
	{
		if (FVBCombatGroup* Found = Coordinator.FindGroupForMemberMutable(Member))
		{
			Coordinator.RecomputeGroup(*Found);
		}
	}

	static void SetAttackKey(AVBAIController& Controller, FName Key) { Controller.AttackRangeKeyName = Key; }
	static bool Reported(const AVBAIController& Controller) { return Controller.bReportedAttackRangeWriteFailure; }
};

namespace VBCombatAuditTests
{
	struct FWorldFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		FWorldFixture()
		{
			const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
				.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(true)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
				ERHIFeatureLevel::Num, &Init);
			if (World)
			{
				Package.Reset(World->GetPackage());
			}
		}

		~FWorldFixture()
		{
			if (World)
			{
				World->DestroyWorld(false);
			}

			if (Package.IsValid())
			{
				Package->SetDirtyFlag(false);
			}
		}
	};

	// 한 대상(원점)을 둘러싼 시험 적 무리. 토큰 총량은 이 시험 동안만 바꾸고 되돌린다(메모리만, 설정 파일 쓰기 없음).
	struct FGroupFixture : FWorldFixture
	{
		AActor* Target = nullptr;
		UVBCombatCoordinatorSubsystem* Coordinator = nullptr;
		const int32 PreviousTokens = GetDefault<UVBCombatSettings>()->MaxAttackTokens;

		explicit FGroupFixture(int32 Tokens) { GetMutableDefault<UVBCombatSettings>()->MaxAttackTokens = Tokens; }
		~FGroupFixture() { GetMutableDefault<UVBCombatSettings>()->MaxAttackTokens = PreviousTokens; }

		bool Init(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("isolated combat world"), World))
			{
				return false;
			}

			Target = World->SpawnActor<AActor>();
			Coordinator = World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
			return Test.TestNotNull(TEXT("target"), Target) && Test.TestNotNull(TEXT("coordinator"), Coordinator);
		}

		// 대상에서 X 만큼 떨어진 준비된 적(사거리 180, 놓는 거리 200, 자산 기본값) - 대상 그룹에 든다.
		UVBEnemyCombatComponent* Spawn(double X)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			AVBCombatAuditTestEnemy* Enemy = World->SpawnActor<AVBCombatAuditTestEnemy>(FVector(X, 0.0, 0.0),
				FRotator::ZeroRotator, Params);
			if (!Enemy)
			{
				return nullptr;
			}

			UVBEnemyConfig* Config = NewObject<UVBEnemyConfig>(Enemy);
			Config->AttackAbilityInputTag = VBGameplayTags::Input_Attack_Light;
			Enemy->Configure(Config);
			UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
			UVBEnemyCombatComponent* Combat = Enemy->GetCombatComponent();
			if (!ASC || !Combat)
			{
				return nullptr;
			}

			ASC->InitAbilityActorInfo(Enemy, Enemy);
			ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatAuditTestAbility::StaticClass()));
			Combat->SetCombatTarget(Target);
			return Combat;
		}

		static void MoveTo(UVBEnemyCombatComponent* Member, double X) { Member->GetOwner()->SetActorLocation(FVector(X, 0.0, 0.0)); }
		bool Holds(UVBEnemyCombatComponent* Member) const { return Coordinator->GetHeldTokenCost(Member) > 0; }
		EVBCombatRole Role(UVBEnemyCombatComponent* Member) const { return Coordinator->GetAssignedRole(Member); }
		void Recompute(UVBEnemyCombatComponent* AnyMember) { FVBCombatAuditTestAccess::Recompute(*Coordinator, AnyMember); }
	};

	void AddBlackboardEntry(UBlackboardData& Data, FName Name, UBlackboardKeyType* Type)
	{
		FBlackboardEntry Entry;
		Entry.EntryName = Name;
		Entry.KeyType = Type;
		Data.Keys.Add(Entry);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatApproachLeaseBoundaryTest,
	"VB.Unit.Combat.ApproachLeaseBoundary",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatApproachLeaseBoundaryTest::RunTest(const FString& Parameters)
{
	VBCombatAuditTests::FWorldFixture Fixture;
	if (!TestNotNull(TEXT("isolated combat world"), Fixture.World))
	{
		return false;
	}

	AVBCombatAuditTestEnemy* Enemy = Fixture.World->SpawnActor<AVBCombatAuditTestEnemy>();
	AActor* Target = Fixture.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("enemy"), Enemy) || !TestNotNull(TEXT("target"), Target))
	{
		return false;
	}

	UVBEnemyConfig* Config = NewObject<UVBEnemyConfig>(Enemy);
	Config->AttackAbilityInputTag = VBGameplayTags::Input_Attack_Light;
	Enemy->Configure(Config);
	UAbilitySystemComponent* ASC = Enemy->GetAbilitySystemComponent();
	UVBEnemyCombatComponent* Combat = Enemy->GetCombatComponent();
	UVBCombatCoordinatorSubsystem* Coordinator = Fixture.World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	if (!TestNotNull(TEXT("real ASC"), ASC) || !TestNotNull(TEXT("combat"), Combat)
		|| !TestNotNull(TEXT("coordinator"), Coordinator))
	{
		return false;
	}

	ASC->InitAbilityActorInfo(Enemy, Enemy);
	ASC->GiveAbility(FGameplayAbilitySpec(UVBCombatAuditTestAbility::StaticClass()));
	Combat->SetCombatTarget(Target);
	if (!TestTrue(TEXT("public token acquisition"), Coordinator->TryAcquireAttackToken(Combat, 1)))
	{
		return false;
	}

	FVBCombatGroup* Group = FVBCombatAuditTestAccess::Group(*Coordinator, Combat);
	FVBCombatMemberState* State = Group ? Group->Members.Find(Combat) : nullptr;
	if (!TestNotNull(TEXT("registered lease state"), State))
	{
		return false;
	}

	const TWeakObjectPtr<UVBEnemyCombatComponent> MemberKey(Combat);
	const TWeakObjectPtr<UVBEnemyCombatComponent> ExistingKey;
	TArray<TWeakObjectPtr<UVBEnemyCombatComponent>> Queue;
	for (bool bUseHelper : {false, true})
	{
		State->HeldTokenCost = 1;
		State->AttackStartedTime = 12.0;
		State->DistanceAtGrant = 567.0;
		State->bReportedHoldOverrun = true;
		State->TokenGrantedTime = 99.0;
		State->Role = EVBCombatRole::Surrounder;
		State->SlotIndex = 7;
		State->SlotLocation = FVector(10.0, 20.0, 30.0);
		Group->NextGrantAllowedTime = 17.5;
		Queue.Reset();
		Queue.Add(ExistingKey);
		if (!TestFalse(TEXT("readiness is false until real lease clears"), Combat->IsReadyToAttack()))
		{
			return false;
		}

		if (bUseHelper)
		{
			FVBCombatAuditTestAccess::Revoke(MemberKey, *State, Queue);
		}
		else
		{
			FVBCombatAuditTestAccess::OriginalRevoke(MemberKey, *State, Queue);
		}

		TestEqual(TEXT("clears token cost"), State->HeldTokenCost, 0);
		TestEqual(TEXT("clears attack clock"), State->AttackStartedTime, -1.0);
		TestEqual(TEXT("clears grant distance"), State->DistanceAtGrant, -1.0);
		TestFalse(TEXT("clears overrun warning"), State->bReportedHoldOverrun);
		TestEqual(TEXT("grant timestamp unchanged"), State->TokenGrantedTime, 99.0);
		TestTrue(TEXT("role unchanged"), State->Role == EVBCombatRole::Surrounder);
		TestEqual(TEXT("slot index unchanged"), State->SlotIndex, 7);
		TestEqual(TEXT("slot location unchanged"), State->SlotLocation, FVector(10.0, 20.0, 30.0));
		TestEqual(TEXT("group pause unchanged"), Group->NextGrantAllowedTime, 17.5);
		TestTrue(TEXT("now ready"), Combat->IsReadyToAttack());
		if (TestEqual(TEXT("ready member appended"), Queue.Num(), 2))
		{
			TestTrue(TEXT("existing order unchanged"), Queue[0] == ExistingKey);
			TestTrue(TEXT("revoked member goes last"), Queue[1] == MemberKey);
		}
	}

	ASC->ClearAllAbilities();
	State->HeldTokenCost = 1;
	Queue.Reset();
	Queue.Add(ExistingKey);
	FVBCombatAuditTestAccess::Revoke(MemberKey, *State, Queue);
	TestEqual(TEXT("not ready still loses lease"), State->HeldTokenCost, 0);
	TestEqual(TEXT("not ready is not appended"), Queue.Num(), 1);
	Combat->LeaveCombat();
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttackRangeWriteContractTest,
	"VB.Unit.Combat.AttackRangeWriteContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAttackRangeWriteContractTest::RunTest(const FString& Parameters)
{
	VBCombatAuditTests::FWorldFixture Fixture;
	if (!TestNotNull(TEXT("isolated AI world"), Fixture.World))
	{
		return false;
	}

	AVBAIController* Controller = Fixture.World->SpawnActor<AVBAIController>();
	if (!TestNotNull(TEXT("controller"), Controller))
	{
		return false;
	}

	TestNull(TEXT("no blackboard before setup"), Controller->GetBlackboardComponent());
	Controller->PushAttackStartDistance(1.0f);
	TestFalse(TEXT("absent BB is not a failure episode"), FVBCombatAuditTestAccess::Reported(*Controller));
	UBlackboardData* Data = NewObject<UBlackboardData>(Controller);
	VBCombatAuditTests::AddBlackboardEntry(*Data, TEXT("AttackRange"), NewObject<UBlackboardKeyType_Float>(Data));
	VBCombatAuditTests::AddBlackboardEntry(*Data, TEXT("WrongType"), NewObject<UBlackboardKeyType_Int>(Data));
	UBlackboardComponent* BB = nullptr;
	if (!TestTrue(TEXT("real blackboard initializes"), Controller->UseBlackboard(Data, BB))
		|| !TestNotNull(TEXT("blackboard"), BB))
	{
		return false;
	}

	Controller->PushAttackStartDistance(245.0f);
	TestEqual(TEXT("normal Float receives distance"), BB->GetValueAsFloat(TEXT("AttackRange")), 245.0f);
	BB->SetValueAsInt(TEXT("WrongType"), 7);
	AddExpectedError(TEXT("Attack range blackboard write failed:"), EAutomationExpectedErrorFlags::Contains, 3);
	FVBCombatAuditTestAccess::SetAttackKey(*Controller, TEXT("WrongType"));
	Controller->PushAttackStartDistance(10.0f);
	Controller->PushAttackStartDistance(20.0f);
	TestTrue(TEXT("wrong type marks episode"), FVBCombatAuditTestAccess::Reported(*Controller));
	TestEqual(TEXT("wrong typed value unchanged"), BB->GetValueAsInt(TEXT("WrongType")), 7);
	FVBCombatAuditTestAccess::SetAttackKey(*Controller, TEXT("Missing"));
	Controller->PushAttackStartDistance(30.0f);
	FVBCombatAuditTestAccess::SetAttackKey(*Controller, TEXT("AttackRange"));
	Controller->PushAttackStartDistance(245.0f);
	TestFalse(TEXT("unchanged valid write rearms warnings"), FVBCombatAuditTestAccess::Reported(*Controller));
	FVBCombatAuditTestAccess::SetAttackKey(*Controller, TEXT("Missing"));
	Controller->PushAttackStartDistance(40.0f);
	Controller->PushAttackStartDistance(50.0f);
	TestTrue(TEXT("missing key marks new episode"), FVBCombatAuditTestAccess::Reported(*Controller));
	TestEqual(TEXT("failure does not overwrite valid key"), BB->GetValueAsFloat(TEXT("AttackRange")), 245.0f);
	AVBAIController* Other = Fixture.World->SpawnActor<AVBAIController>();
	if (!TestNotNull(TEXT("second controller"), Other))
	{
		return false;
	}

	UBlackboardComponent* OtherBB = nullptr;
	if (!TestTrue(TEXT("second blackboard initializes"), Other->UseBlackboard(Data, OtherBB)))
	{
		return false;
	}

	FVBCombatAuditTestAccess::SetAttackKey(*Other, TEXT("Missing"));
	Other->PushAttackStartDistance(60.0f);
	TestTrue(TEXT("warning state is controller local"), FVBCombatAuditTestAccess::Reported(*Other));
	return true;
}
// 누가 공격하나(RecomputeGroup)의 현재 동작을 고정한다 - 단계를 나누기 전과 뒤가 같아야 한다. 토큰 하나.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRecomputeCharacterisationTest,
	"VB.Unit.Combat.RecomputeCharacterisation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRecomputeCharacterisationTest::RunTest(const FString& Parameters)
{
	using VBCombatAuditTests::FGroupFixture;
	{
		// 순번: 놓는 거리(200) 밖으로 물러난 쪽이 앞, 그중 가까운 쪽.
		FGroupFixture Fixture(1);
		if (!Fixture.Init(*this))
		{
			return false;
		}

		UVBEnemyCombatComponent* Near = Fixture.Spawn(150.0);
		UVBEnemyCombatComponent* Mid = Fixture.Spawn(300.0);
		UVBEnemyCombatComponent* Far = Fixture.Spawn(450.0);
		if (!TestTrue(TEXT("three enemies"), Near && Mid && Far))
		{
			return false;
		}

		Fixture.Recompute(Near);
		TestTrue(TEXT("order: the nearest retreated member gets the token"), Fixture.Holds(Mid));
		TestFalse(TEXT("order: not the one still inside release"), Fixture.Holds(Near));
		TestFalse(TEXT("order: not the farther retreated one"), Fixture.Holds(Far));
		TestTrue(TEXT("role: token holder outside start distance approaches"), Fixture.Role(Mid) == EVBCombatRole::Approaching);
		TestTrue(TEXT("role: no token waits"), Fixture.Role(Near) == EVBCombatRole::Surrounder);
		Fixture.Recompute(Near);
		TestTrue(TEXT("a holder that has not moved keeps the token"), Fixture.Holds(Mid));
		for (UVBEnemyCombatComponent* Member : {Near, Mid, Far}) Member->LeaveCombat();
	}

	{
		// 위치 기준 이동: 공격 전 보유자가 부여 때보다 멀어지고 사거리 안 후보가 있으면 토큰이 옮겨 간다.
		FGroupFixture Fixture(1);
		if (!Fixture.Init(*this))
		{
			return false;
		}

		UVBEnemyCombatComponent* Holder = Fixture.Spawn(300.0);
		if (!TestNotNull(TEXT("holder"), Holder))
		{
			return false;
		}

		Fixture.Recompute(Holder);
		if (!TestTrue(TEXT("holder granted alone"), Fixture.Holds(Holder)))
		{
			return false;
		}

		UVBEnemyCombatComponent* InRange = Fixture.Spawn(150.0);
		if (!TestNotNull(TEXT("in-range candidate"), InRange))
		{
			return false;
		}

		FGroupFixture::MoveTo(Holder, 250.0);
		Fixture.Recompute(Holder);
		TestTrue(TEXT("came closer than at grant: keeps the token"), Fixture.Holds(Holder));
		FGroupFixture::MoveTo(Holder, 500.0);
		Fixture.Recompute(Holder);
		TestFalse(TEXT("moved away beyond release and grant distance: loses the token"), Fixture.Holds(Holder));
		TestTrue(TEXT("moved away: the in-range candidate gets it"), Fixture.Holds(InRange));
		for (UVBEnemyCombatComponent* Member : {Holder, InRange}) Member->LeaveCombat();
	}

	{
		// 게이트: 예고 중인 멤버가 있거나 그룹 숨 동안은 아무에게도 주지 않는다.
		FGroupFixture Fixture(1);
		if (!Fixture.Init(*this))
		{
			return false;
		}

		UVBEnemyCombatComponent* Waiting = Fixture.Spawn(300.0);
		UVBEnemyCombatComponent* Telegraphing = Fixture.Spawn(450.0);
		if (!TestTrue(TEXT("two enemies"), Waiting && Telegraphing))
		{
			return false;
		}

		UAbilitySystemComponent* TelegraphASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Telegraphing->GetOwner());
		if (!TestNotNull(TEXT("telegraphing ASC"), TelegraphASC))
		{
			return false;
		}

		TelegraphASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing);
		Fixture.Recompute(Waiting);
		TestFalse(TEXT("telegraph gate: nobody is granted"), Fixture.Holds(Waiting) || Fixture.Holds(Telegraphing));
		TelegraphASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Telegraphing);
		FVBCombatAuditTestAccess::Group(*Fixture.Coordinator, Waiting)->NextGrantAllowedTime = 1.0e6;
		Fixture.Recompute(Waiting);
		TestFalse(TEXT("pause gate: nobody is granted"), Fixture.Holds(Waiting) || Fixture.Holds(Telegraphing));
		FVBCombatAuditTestAccess::Group(*Fixture.Coordinator, Waiting)->NextGrantAllowedTime = 0.0;
		Fixture.Recompute(Waiting);
		TestTrue(TEXT("gates open: the order rule grants again"), Fixture.Holds(Waiting));
		for (UVBEnemyCombatComponent* Member : {Waiting, Telegraphing}) Member->LeaveCombat();
	}

	{
		// 역할 히스테리시스: 사거리(180) 안에 들어와야 Attacker, 놓는 거리(200) 밖으로 나가야 놓는다.
		FGroupFixture Fixture(1);
		if (!Fixture.Init(*this))
		{
			return false;
		}

		UVBEnemyCombatComponent* Alone = Fixture.Spawn(300.0);
		if (!TestNotNull(TEXT("single enemy"), Alone))
		{
			return false;
		}

		Fixture.Recompute(Alone);
		TestTrue(TEXT("outside start: approaching"), Fixture.Role(Alone) == EVBCombatRole::Approaching);
		FGroupFixture::MoveTo(Alone, 170.0);
		Fixture.Recompute(Alone);
		TestTrue(TEXT("inside start: attacker"), Fixture.Role(Alone) == EVBCombatRole::Attacker);
		FGroupFixture::MoveTo(Alone, 195.0);
		Fixture.Recompute(Alone);
		TestTrue(TEXT("between start and release: stays attacker"), Fixture.Role(Alone) == EVBCombatRole::Attacker);
		FGroupFixture::MoveTo(Alone, 210.0);
		Fixture.Recompute(Alone);
		TestTrue(TEXT("beyond release: approaching again"), Fixture.Role(Alone) == EVBCombatRole::Approaching);
		Alone->LeaveCombat();
	}

	return true;
}
#endif
