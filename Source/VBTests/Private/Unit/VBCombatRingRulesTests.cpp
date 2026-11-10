#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "AI/VBCombatRingRules.h"
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Data/VBCombatSettings.h"
#include "VBCombatAuditTestAbility.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS
namespace VBCombatRingTests
{
	struct FAdapterFixture
	{
		UWorld* World = nullptr;
		TStrongObjectPtr<UPackage> Package;
		AActor* Target = nullptr;
		TArray<AVBCombatAuditTestEnemy*> Enemies;
		TArray<double> Radii;
		~FAdapterFixture()
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

	class FObserveAdapter : public IAutomationLatentCommand
	{
	public:
		FObserveAdapter(FAutomationTestBase* InTest, TSharedPtr<FAdapterFixture> InFixture)
			: Test(InTest), Fixture(MoveTemp(InFixture)) {}
		virtual bool Update() override
		{
			if (LastFrame == GFrameCounter)
			{
				return false;
			}

			LastFrame = GFrameCounter;
			const float Interval = FMath::Max(GetDefault<UVBCombatSettings>()->SlotRecomputeInterval, KINDA_SMALL_NUMBER);
			Fixture->World->GetTimerManager().Tick(Interval + 0.01f);
			// 첫 tick 은 대기 타이머를 올리고, 두 번째 엔진 프레임에서 실행된다
			if (++Frames < 2)
			{
				return false;
			}

			UVBCombatCoordinatorSubsystem* Coordinator = Fixture->World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
			for (int32 Index = 0; Index < Fixture->Enemies.Num(); ++Index)
			{
				const UVBEnemyCombatComponent* Combat = Fixture->Enemies[Index]->GetCombatComponent();
				Test->TestEqual(TEXT("public registration reached surrounder role"), Coordinator->GetAssignedRole(Combat),
					EVBCombatRole::Surrounder);
				const FVector Slot = Coordinator->GetAssignedSlotLocation(Combat);
				Test->TestTrue(TEXT("adapter applies the radius belonging to this enemy"),
					FMath::IsNearlyEqual(FVector::Dist(Slot, Fixture->Target->GetActorLocation()), Fixture->Radii[Index], 1.e-7));
				Test->TestFalse(TEXT("assignment replaced the member's own location"),
					Slot.Equals(Fixture->Enemies[Index]->GetActorLocation(), 1.e-7));
			}

			return true;
		}
	private:
		FAutomationTestBase* Test;
		TSharedPtr<FAdapterFixture> Fixture;
		uint64 LastFrame = MAX_uint64;
		int32 Frames = 0;
	};

	FVBCombatRingMember Member(const FVector& Location, double Radius)
	{
		FVBCombatRingMember Result;
		Result.Location = Location;
		Result.WaitingDistance = Radius;
		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRingGeometryTest, "VB.Unit.CombatRing.Geometry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBCombatRingGeometryTest::RunTest(const FString& Parameters)
{
	TArray<FVBCombatRingMember> Members;
	TestEqual(TEXT("empty input"), VBCombatRingRules::AssignSlots(FVector::ZeroVector, 4, Members).Num(), 0);
	const FVector Center(17.0, -25.0, 30.0);
	Members.Add(VBCombatRingTests::Member(Center + FVector(110.0, 1.0, 0.0), 100.0));
	auto Assigned = VBCombatRingRules::AssignSlots(Center, 4, Members);
	if (!TestEqual(TEXT("one member assigned"), Assigned.Num(), 1))
	{
		return false;
	}

	TestEqual(TEXT("world +X slot"), Assigned[0].SlotIndex, 0);
	TestTrue(TEXT("translated center and radius"), Assigned[0].Location.Equals(Center + FVector(100.0, 0.0, 0.0), 1.e-8));

	Members = {VBCombatRingTests::Member(FVector(220.0, 0.0, 0.0), 200.0),
		VBCombatRingTests::Member(FVector(0.0, 430.0, 0.0), 400.0)};
	Assigned = VBCombatRingRules::AssignSlots(FVector::ZeroVector, 4, Members);
	if (!TestEqual(TEXT("both members assigned"), Assigned.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("first accepted member"), Assigned[0].MemberIndex, 0);
	TestEqual(TEXT("first accepted slot"), Assigned[0].SlotIndex, 0);
	TestEqual(TEXT("second accepted member"), Assigned[1].MemberIndex, 1);
	TestEqual(TEXT("second accepted slot"), Assigned[1].SlotIndex, 1);
	TestTrue(TEXT("first radius retained"), FMath::IsNearlyEqual(Assigned[0].Location.Size(), 200.0, 1.e-8));
	TestTrue(TEXT("second radius retained"), FMath::IsNearlyEqual(Assigned[1].Location.Size(), 400.0, 1.e-8));

	// 슬롯 하나로 3D 비용만 본다 - XY 로 가까운 멤버가 Z 로는 훨씬 멀다
	Members = {VBCombatRingTests::Member(FVector(0.0, 0.0, 100.0), 0.0),
		VBCombatRingTests::Member(FVector(1.0, 0.0, 0.0), 0.0)};
	for (const int32 Count : {1, 0, -2})
	{
		Assigned = VBCombatRingRules::AssignSlots(FVector::ZeroVector, Count, Members);
		if (!TestEqual(TEXT("one slot including nonpositive fallback"), Assigned.Num(), 1))
		{
			return false;
		}

		TestEqual(TEXT("3D nearest member wins"), Assigned[0].MemberIndex, 1);
		TestEqual(TEXT("single slot index"), Assigned[0].SlotIndex, 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRingTieTest, "VB.Unit.CombatRing.ExactTie",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBCombatRingTieTest::RunTest(const FString& Parameters)
{
	// 반지름 0 이면 sin/cos 반올림까지 포함해 모든 비용이 정확히 0 이다
	const TArray<FVBCombatRingMember> Members = {
		VBCombatRingTests::Member(FVector::ZeroVector, 0.0),
		VBCombatRingTests::Member(FVector::ZeroVector, 0.0),
		VBCombatRingTests::Member(FVector::ZeroVector, 0.0)};
	const auto Assigned = VBCombatRingRules::AssignSlots(FVector::ZeroVector, 2, Members);
	if (!TestEqual(TEXT("excess member has no invented slot"), Assigned.Num(), 2))
	{
		return false;
	}

	TestEqual(TEXT("tie chooses first input first"), Assigned[0].MemberIndex, 0);
	TestEqual(TEXT("tie chooses lowest slot first"), Assigned[0].SlotIndex, 0);
	TestEqual(TEXT("next input selected exactly once"), Assigned[1].MemberIndex, 1);
	TestEqual(TEXT("next slot selected exactly once"), Assigned[1].SlotIndex, 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRingAdapterTest, "VB.Unit.CombatRing.Adapter",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBCombatRingAdapterTest::RunTest(const FString& Parameters)
{
	const TSharedPtr<VBCombatRingTests::FAdapterFixture> Fixture = MakeShared<VBCombatRingTests::FAdapterFixture>();
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(true).CreateNavigation(false).CreateAISystem(false).ShouldSimulatePhysics(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("adapter world"), World))
	{
		return false;
	}

	Fixture->World = World;
	Fixture->Package.Reset(World->GetPackage());
	World->InitializeActorsForPlay(FURL());
	UVBCombatCoordinatorSubsystem* Coordinator = World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
	if (!TestNotNull(TEXT("real coordinator"), Coordinator))
	{
		return false;
	}

	TestNull(TEXT("no NavMesh projection in geometry fixture"), World->GetNavigationSystem());
	AActor* Target = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("target"), Target))
	{
		return false;
	}

	Fixture->Target = Target;
	const FVector Positions[] = {FVector(530.0, 17.0, 0.0), FVector(-41.0, 710.0, 0.0)};
	TArray<AVBCombatAuditTestEnemy*>& Enemies = Fixture->Enemies;
	TArray<double>& Radii = Fixture->Radii;
	for (int32 Index = 0; Index < 2; ++Index)
	{
		AVBCombatAuditTestEnemy* Enemy = World->SpawnActor<AVBCombatAuditTestEnemy>(Positions[Index], FRotator::ZeroRotator);
		if (!TestNotNull(TEXT("concrete enemy"), Enemy))
		{
			return false;
		}

		UVBEnemyConfig* Config = NewObject<UVBEnemyConfig>(Enemy);
		Config->AttackCommitDistance = Index == 0 ? 180.25f : 240.1f;
		Config->AttackReleaseDistance = Index == 0 ? 220.0f : 260.0f;
		Config->WaitingStandoff = Index == 0 ? 70.125f : 120.2f;
		Config->AttackAbilityInputTag = FGameplayTag();
		Enemy->Configure(Config);
		Radii.Add(static_cast<double>(Config->AttackCommitDistance + Config->WaitingStandoff));
		Enemies.Add(Enemy);
		Enemy->GetCombatComponent()->SetCombatTarget(Target);
	}

	TestTrue(TEXT("fixture radii differ"), FMath::Abs(Radii[0] - Radii[1]) > 1.0);
	const double DoubleSum = static_cast<double>(Enemies[1]->GetEnemyConfig()->AttackCommitDistance)
		+ static_cast<double>(Enemies[1]->GetEnemyConfig()->WaitingStandoff);
	TestTrue(TEXT("fixture detects widening before float addition"), FMath::Abs(DoubleSum - Radii[1]) > 1.e-6);
	ADD_LATENT_AUTOMATION_COMMAND(VBCombatRingTests::FObserveAdapter(this, Fixture));
	return true;
}
#endif
