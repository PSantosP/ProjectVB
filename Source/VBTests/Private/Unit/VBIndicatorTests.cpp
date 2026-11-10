// ProjectVB Copyright (c) 2025. All Rights Reserved.

// 표시 요청 목록, 고르기의 단위 시험. 위젯, Slate, 소스 어댑터, 원격 배달은 여기서 보지 않는다.
// 음성 대조: 만료된 OneViewer 를 모두로 -> Registry.ExpiredOneViewer*, 지운 번호 재사용 -> Registry.IdNeverReused,
//   목록 식별 무시 -> Registry.ForeignHandle*, 고치기를 지우고 다시 걸기 -> Registry.UpdateKeepsIdAndOrder 가 실패해야 한다.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/SceneComponent.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"
#include "Unit/VBTestIniTag.h"
#include "UI/Indicator/VBIndicatorSelection.h"
#include "UI/VBEnemyBarRules.h"
#include "UObject/GarbageCollection.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include <limits>
#include <type_traits>

namespace VBIndicatorD01aTests
{
	// 시험 묶음은 ini 태그다(VBTestIniTag.h).
	const FVBTestIniTag TAG_Nearby{TEXT("Indicator.Group.Test.D01a.Nearby")};
	const FVBTestIniTag TAG_Target{TEXT("Indicator.Group.Test.D01a.Target")};
	const FVBTestIniTag TAG_Boss{TEXT("Indicator.Group.Test.D01a.Boss")};

	constexpr auto Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	static_assert(!std::is_copy_constructible_v<FVBIndicatorRegistration>);
	static_assert(!std::is_copy_assignable_v<FVBIndicatorRegistration>);
	static_assert(std::is_nothrow_move_constructible_v<FVBIndicatorRegistration>);
	static_assert(std::is_nothrow_move_assignable_v<FVBIndicatorRegistration>);

	struct FWorldFixture
	{
		UWorld* World = nullptr;
		UVBIndicatorRegistrySubsystem* Registry = nullptr;
		APlayerController* ViewerA = nullptr;
		APlayerController* ViewerB = nullptr;

		~FWorldFixture() { Destroy(); }
		void Destroy()
		{
			if (World)
			{
				UPackage* Package = World->GetPackage();
				World->DestroyWorld(false);
				Package->SetDirtyFlag(false);
			}

			World = nullptr;
			Registry = nullptr;
			ViewerA = ViewerB = nullptr;
		}

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Test.TestNotNull(TEXT("Fixture.Engine"), GEngine))
			{
				return false;
			}

			const UWorld::InitializationValues Init = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
				.ShouldSimulatePhysics(false).SetTransactional(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
			if (!Test.TestNotNull(TEXT("Fixture.World"), World))
			{
				return false;
			}

			Registry = World->GetSubsystem<UVBIndicatorRegistrySubsystem>();
			ViewerA = World->SpawnActor<APlayerController>();
			ViewerB = World->SpawnActor<APlayerController>();
			return Test.TestNotNull(TEXT("Fixture.RealWorldSubsystem"), Registry)
				&& Test.TestNotNull(TEXT("Fixture.ViewerA"), ViewerA)
				&& Test.TestNotNull(TEXT("Fixture.ViewerB"), ViewerB);
		}

		USceneComponent* MakeAnchor(FAutomationTestBase& Test, AActor* ExistingOwner = nullptr)
		{
			AActor* Owner = ExistingOwner ? ExistingOwner : World->SpawnActor<AActor>();
			if (!Test.TestNotNull(TEXT("Fixture.AnchorOwner"), Owner))
			{
				return nullptr;
			}

			USceneComponent* Anchor = NewObject<USceneComponent>(Owner);
			if (!Test.TestNotNull(TEXT("Fixture.Anchor"), Anchor))
			{
				return nullptr;
			}

			Owner->AddInstanceComponent(Anchor);
			if (!Owner->GetRootComponent())
			{
				Owner->SetRootComponent(Anchor);
			}

			Anchor->RegisterComponent();
			return Anchor;
		}
	};

	FVBIndicatorRequest MakeRequest(USceneComponent* Anchor, APlayerController* Viewer = nullptr)
	{
		FVBIndicatorRequest Request;
		Request.Group = TAG_Nearby;
		Request.AnchorComponent = Anchor;
		Request.Subject = Anchor ? Anchor->GetOwner() : nullptr;
		if (Viewer)
		{
			Request.ViewerScope = EVBIndicatorViewerScope::OneViewer;
			Request.Viewer = Viewer;
		}

		return Request;
	}

	TArray<uint64> Ids(TConstArrayView<FVBIndicatorHandle> Handles)
	{
		TArray<uint64> Result;
		for (const FVBIndicatorHandle& Handle : Handles)
		{
			Result.Add(Handle.GetId());
		}

		return Result;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBIndicatorRegistryD01aTest, "VB.Unit.Indicator.Registry", VBIndicatorD01aTests::Flags)
bool FVBIndicatorRegistryD01aTest::RunTest(const FString& Parameters)
{
	using namespace VBIndicatorD01aTests;
	FWorldFixture F;
	FWorldFixture Other;
	if (!F.Initialize(*this) || !Other.Initialize(*this))
	{
		return false;
	}

	USceneComponent* Anchor = F.MakeAnchor(*this);
	USceneComponent* OtherAnchor = Other.MakeAnchor(*this);
	if (!Anchor || !OtherAnchor)
	{
		return false;
	}

	FVBIndicatorRequest All = MakeRequest(Anchor);
	const FVBIndicatorHandle H1 = F.Registry->Add(All);
	const FVBIndicatorHandle HA = F.Registry->Add(MakeRequest(Anchor, F.ViewerA));
	const FVBIndicatorHandle HB = F.Registry->Add(MakeRequest(Anchor, F.ViewerB));
	TestTrue(TEXT("Registry.AddLive"), H1.IsValid() && HA.IsValid() && HB.IsValid());
	const TArray<uint64> ExpectedA = {H1.GetId(), HA.GetId()};
	const TArray<uint64> ExpectedB = {H1.GetId(), HB.GetId()};
	TestTrue(TEXT("Registry.ViewerA.RegistrationOrder"), Ids(F.Registry->SnapshotForViewer(F.ViewerA)) == ExpectedA);
	TestTrue(TEXT("Registry.ViewerB.Scope"), Ids(F.Registry->SnapshotForViewer(F.ViewerB)) == ExpectedB);
	TestEqual(TEXT("Registry.NullViewer.NoDisplay"), F.Registry->SnapshotForViewer(nullptr).Num(), 0);
	TestEqual(TEXT("Registry.ForeignViewer.NoDisplay"), F.Registry->SnapshotForViewer(Other.ViewerA).Num(), 0);

	FVBIndicatorRequest Invalid = All;
	Invalid.Group = {};
	TestFalse(TEXT("Registry.InvalidGroupRejected"), F.Registry->Add(Invalid).IsValid());
	Invalid = All;
	Invalid.AnchorComponent.Reset();
	TestFalse(TEXT("Registry.NoSourceRejected"), F.Registry->Add(Invalid).IsValid());
	Invalid = MakeRequest(OtherAnchor);
	TestFalse(TEXT("Registry.ForeignSourceRejected"), F.Registry->Add(Invalid).IsValid());
	Invalid = All;
	Invalid.ViewerScope = EVBIndicatorViewerScope::OneViewer;
	TestFalse(TEXT("Registry.UnsetOneViewerNeverBroadens"), F.Registry->Add(Invalid).IsValid());
	Invalid.Viewer = Other.ViewerA;
	TestFalse(TEXT("Registry.ForeignScopedViewerRejected"), F.Registry->Add(Invalid).IsValid());
	Invalid = All;
	Invalid.ViewerScope = static_cast<EVBIndicatorViewerScope>(255);
	TestFalse(TEXT("Registry.InvalidScopeRejected"), F.Registry->Add(Invalid).IsValid());

	All.Tint = FLinearColor::Red;
	All.Priority = 99;
	All.AnchorSocket = TEXT("MarkerSocket");
	All.WorldOffset = FVector(1, 2, 3);
	All.bSuppressed = true;
	TestTrue(TEXT("Registry.UpdateInPlace"), F.Registry->Update(H1, All));
	FVBIndicatorRequest Copy;
	TestTrue(TEXT("Registry.ReadCopy"), F.Registry->TryGet(H1, Copy));
	TestTrue(TEXT("Registry.MetadataUpdated"), Copy.Tint == All.Tint && Copy.Priority == 99
		&& Copy.AnchorSocket == All.AnchorSocket && Copy.WorldOffset == All.WorldOffset && Copy.bSuppressed);
	TestTrue(TEXT("Registry.UpdateKeepsIdAndOrder"), Ids(F.Registry->SnapshotForViewer(F.ViewerA)) == ExpectedA);
	Copy.Tint = FLinearColor::Green;
	F.Registry->TryGet(H1, Copy);
	TestTrue(TEXT("Registry.CallerCopyCannotMutateEntry"), Copy.Tint == FLinearColor::Red);
	TestTrue(TEXT("Registry.ScopedReadAllowed"), F.Registry->TryGetForViewer(HA, F.ViewerA, Copy));
	TestFalse(TEXT("Registry.ScopedReadDenied"), F.Registry->TryGetForViewer(HA, F.ViewerB, Copy));
	TestFalse(TEXT("Registry.DeniedReadClearsCopy"), Copy.Group.IsValid());
	Invalid = All;
	Invalid.AnchorComponent.Reset();
	TestFalse(TEXT("Registry.InvalidUpdateRejected"), F.Registry->Update(H1, Invalid));
	TestTrue(TEXT("Registry.InvalidUpdateKeepsOld"), H1.IsValid());

	const FVBIndicatorHandle Stale = HA;
	TestTrue(TEXT("Registry.FirstRemove"), F.Registry->Remove(HA));
	TestFalse(TEXT("Registry.SecondRemoveHarmless"), F.Registry->Remove(HA));
	const FVBIndicatorHandle New = F.Registry->Add(MakeRequest(Anchor, F.ViewerA));
	TestTrue(TEXT("Registry.IdNeverReused"), New.GetId() > HB.GetId());
	TestFalse(TEXT("Registry.StaleAliasInvalid"), Stale.IsValid());
	TestFalse(TEXT("Registry.StaleUpdateCannotChangeNew"), F.Registry->Update(Stale, All));
	TestFalse(TEXT("Registry.StaleRemoveCannotDeleteNew"), F.Registry->Remove(Stale));
	TestTrue(TEXT("Registry.NewSurvivesStaleAlias"), New.IsValid());
	const FVBIndicatorHandle Foreign = Other.Registry->Add(MakeRequest(OtherAnchor));
	TestEqual(TEXT("Registry.SameNumericIdAcrossRegistries"), Foreign.GetId(), H1.GetId());
	TestFalse(TEXT("Registry.IdentityNotJustNumber"), Foreign == H1);
	TestFalse(TEXT("Registry.ForeignHandleNotRegistered"), F.Registry->IsRegistered(Foreign));
	TestFalse(TEXT("Registry.ForeignHandleCannotRemove"), F.Registry->Remove(Foreign));
	TestFalse(TEXT("Registry.ForeignHandleCannotUpdate"), F.Registry->Update(Foreign, All));
	TestTrue(TEXT("Registry.ForeignRegistrationSurvives"), Foreign.IsValid());
	const FVBIndicatorHandle ChangedScope = F.Registry->Add(MakeRequest(Anchor, F.ViewerA));
	const auto OldViewerSnapshot = F.Registry->SnapshotForViewer(F.ViewerA);
	TestTrue(TEXT("Registry.ScopeChangeWasInOldSnapshot"), OldViewerSnapshot.Contains(ChangedScope));
	TestTrue(TEXT("Registry.ScopeUpdateKeepsHandle"), F.Registry->Update(ChangedScope, MakeRequest(Anchor, F.ViewerB)));
	TestFalse(TEXT("Registry.ScopeChangeRejectsOldViewerResolution"), F.Registry->TryGetForViewer(ChangedScope, F.ViewerA, Copy));
	TestTrue(TEXT("Registry.ScopeChangeAllowsNewViewerResolution"), F.Registry->TryGetForViewer(ChangedScope, F.ViewerB, Copy));

	const TArray<FVBIndicatorHandle> Before = F.Registry->SnapshotForViewer(F.ViewerB);
	const TArray<uint64> BeforeIds = Ids(Before);
	for (const FVBIndicatorHandle& Id : Before)
	{
		F.Registry->Remove(Id);
		F.Registry->Add(MakeRequest(Anchor));
	}

	TestTrue(TEXT("Registry.SnapshotUnaffectedByMutations"), Ids(Before) == BeforeIds);
	TestFalse(TEXT("Registry.RemovedSnapshotIdNotResolved"), F.Registry->TryGet(HB, Copy));
	TestFalse(TEXT("Registry.RemovedSnapshotClearsCopy"), Copy.Group.IsValid());

	FVBIndicatorHandle MovedAlias;
	FVBIndicatorHandle ReplacedAlias;
	{
		FVBIndicatorRegistration First(*F.Registry, MakeRequest(Anchor));
		MovedAlias = First.GetHandle();
		FVBIndicatorRegistration Moved(MoveTemp(First));
		TestFalse(TEXT("Registry.RAII.MoveCtorEmptiesSource"), First.IsValid());
		First.Reset();
		TestTrue(TEXT("Registry.RAII.MovedFromResetDoesNotRelease"), MovedAlias.IsValid());
		FVBIndicatorRegistration Replaced(*F.Registry, MakeRequest(Anchor));
		ReplacedAlias = Replaced.GetHandle();
		Replaced = MoveTemp(Moved);
		TestFalse(TEXT("Registry.RAII.MoveAssignReleasesDestination"), ReplacedAlias.IsValid());
		TestFalse(TEXT("Registry.RAII.MoveAssignEmptiesSource"), Moved.IsValid());
		TestTrue(TEXT("Registry.RAII.MoveAssignPreservesIdentity"), Replaced.GetHandle() == MovedAlias);
		FVBIndicatorRegistration* Self = &Replaced;
		Replaced = MoveTemp(*Self);
		TestTrue(TEXT("Registry.RAII.SelfMoveHarmless"), Replaced.IsValid());
		TestTrue(TEXT("Registry.RAII.UpdateKeepsLease"), Replaced.Update(All) && Replaced.GetHandle() == MovedAlias);
	}

	TestFalse(TEXT("Registry.RAII.DestructorReleases"), MovedAlias.IsValid());
	{
		FVBIndicatorRegistration Lease(*F.Registry, MakeRequest(Anchor));
		const FVBIndicatorHandle Alias = Lease.GetHandle();
		Lease.Reset();
		Lease.Reset();
		TestFalse(TEXT("Registry.RAII.ResetTwiceHarmless"), Alias.IsValid());
	}

	// 참조 없는 실제 component 를 GC 로 수거한다 - strong ref 를 우회하려고 garbage 로 강제 표시하지 않는다.
	AActor* LooseOwner = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Fixture.LooseOwner"), LooseOwner))
	{
		return false;
	}

	TStrongObjectPtr<USceneComponent> Loose(NewObject<USceneComponent>(LooseOwner));
	LooseOwner->RemoveOwnedComponent(Loose.Get());
	TWeakObjectPtr<USceneComponent> WeakAnchor = Loose.Get();
	TStrongObjectPtr<UObject> Data(NewObject<USceneComponent>());
	TWeakObjectPtr<UObject> WeakData = Data.Get();
	FVBIndicatorRequest WeakRequest = MakeRequest(Loose.Get());
	WeakRequest.Data = Data.Get();
	const FVBIndicatorHandle WeakHandle = F.Registry->Add(WeakRequest);
	FVBIndicatorRequest LiveSourceWeakData = MakeRequest(Anchor);
	LiveSourceWeakData.Data = Data.Get();
	const FVBIndicatorHandle LiveDataHandle = F.Registry->Add(LiveSourceWeakData);
	TestTrue(TEXT("Registry.WeakSourceLiveBeforeGC"), WeakHandle.IsValid());
	Loose.Reset();
	Data.Reset();
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Registry.DoesNotRetainSource"), WeakAnchor.IsValid());
	TestFalse(TEXT("Registry.DoesNotRetainData"), WeakData.IsValid());
	TestFalse(TEXT("Registry.ExpiredSourceRemoved"), F.Registry->IsRegistered(WeakHandle));
	TestTrue(TEXT("Registry.OptionalDataExpiryKeepsLiveSource"), F.Registry->TryGet(LiveDataHandle, Copy) && !Copy.Data.IsValid());

	const FVBIndicatorHandle ExpiringViewer = F.Registry->Add(MakeRequest(Anchor, F.ViewerA));
	FVBIndicatorRequest ExplicitAll = MakeRequest(Anchor);
	ExplicitAll.Viewer = F.ViewerA;
	const FVBIndicatorHandle AllIgnoresViewerField = F.Registry->Add(ExplicitAll);
	F.World->DestroyActor(F.ViewerA);
	TestFalse(TEXT("Registry.ExpiredOneViewerRemoved"), ExpiringViewer.IsValid());
	const auto BSnapshot = F.Registry->SnapshotForViewer(F.ViewerB);
	TestFalse(TEXT("Registry.ExpiredOneViewerNeverBroadens"), BSnapshot.Contains(ExpiringViewer));
	TestTrue(TEXT("Registry.AllScopeIgnoresUnusedViewerExpiry"), AllIgnoresViewerField.IsValid());
	USceneComponent* DyingAnchor = F.MakeAnchor(*this);
	if (!DyingAnchor)
	{
		return false;
	}

	const FVBIndicatorHandle DestroyedSource = F.Registry->Add(MakeRequest(DyingAnchor));
	DyingAnchor->DestroyComponent();
	TestFalse(TEXT("Registry.DestroyComponentRemovesSource"), DestroyedSource.IsValid());

	// 실제 월드 정리 뒤에도 strong 으로 붙든 subsystem - weak owner 유효성만으로는 모자라다.
	TStrongObjectPtr<UVBIndicatorRegistrySubsystem> HeldRegistry(Other.Registry);
	FVBIndicatorRegistration HeldLease(*Other.Registry, MakeRequest(OtherAnchor));
	const FVBIndicatorHandle BeforeDeinit = HeldLease.GetHandle();
	Other.Destroy();
	TestFalse(TEXT("Registry.DeinitInvalidatesHeldBusHandle"), BeforeDeinit.IsValid());
	TestFalse(TEXT("Registry.DeinitRejectsAdd"), HeldRegistry->Add(MakeRequest(Anchor)).IsValid());
	HeldLease.Reset();
	HeldLease.Reset();
	const TWeakObjectPtr<UVBIndicatorRegistrySubsystem> WeakOwnerA = F.Registry;
	const TWeakObjectPtr<UVBIndicatorRegistrySubsystem> WeakOwnerB = HeldRegistry.Get();
	const FVBIndicatorHandle SameExpiredIdentity = H1;
	HeldRegistry.Reset();
	F.Destroy();
	CollectGarbage(RF_NoFlags);
	TestFalse(TEXT("Registry.FixtureOwnerACollected"), WeakOwnerA.IsValid());
	TestFalse(TEXT("Registry.FixtureOwnerBCollected"), WeakOwnerB.IsValid());
	TestFalse(TEXT("Registry.DifferentExpiredOwnersStayDifferent"), H1 == Foreign);
	TestTrue(TEXT("Registry.SameExpiredIdentityStaysEqual"), H1 == SameExpiredIdentity);
	TestEqual(TEXT("Registry.EqualExpiredIdentitySameHash"), GetTypeHash(H1), GetTypeHash(SameExpiredIdentity));
	TSet<FVBIndicatorHandle> ExpiredIdentities;
	ExpiredIdentities.Add(H1);
	ExpiredIdentities.Add(Foreign);
	ExpiredIdentities.Add(SameExpiredIdentity);
	TestEqual(TEXT("Registry.ExpiredIdentitiesSafeAsMapKeys"), ExpiredIdentities.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBIndicatorSelectionD01aTest, "VB.Unit.Indicator.Selection", VBIndicatorD01aTests::Flags)
bool FVBIndicatorSelectionD01aTest::RunTest(const FString& Parameters)
{
	using namespace VBIndicatorD01aTests;
	FWorldFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	TArray<USceneComponent*> Anchors;
	TArray<AActor*> Actors;
	for (int32 Index = 0; Index < 7; ++Index)
	{
		USceneComponent* Anchor = F.MakeAnchor(*this);
		if (!Anchor)
		{
			return false;
		}

		Anchors.Add(Anchor);
		Actors.Add(Anchor->GetOwner());
	}

	const float Distances[] = {900, 300, 600, 2000, 100, 200, 150};
	TArray<FVBNearbyBarCandidate> Legacy;
	TArray<FVBIndicatorSelectionCandidate> Candidates;
	TSet<const AActor*> Dead;
	Dead.Add(Actors[5]);
	for (int32 Index = 0; Index < Actors.Num(); ++Index)
	{
		FVBNearbyBarCandidate Old;
		Old.Actor = Actors[Index];
		Old.DistanceSquared = FMath::Square(Distances[Index]);
		Old.bOnScreen = Index != 4;
		Old.bDead = Index == 5;
		Legacy.Add(Old);
		FVBIndicatorSelectionCandidate Candidate;
		Candidate.Id = uint64(Index + 1);
		Candidate.Request = MakeRequest(Anchors[Index]);
		Candidate.DistanceSquared = Old.DistanceSquared;
		Candidate.bPassesScreenPolicy = Old.bOnScreen;
		Candidates.Add(Candidate);
	}

	auto IsDead = [&Dead](const AActor* Subject) { return Dead.Contains(Subject); };
	FGameplayTagContainer Claiming;
	Claiming.AddTag(TAG_Target);
	Claiming.AddTag(TAG_Boss);
	FVBIndicatorSelectionCandidate Target = Candidates[6];
	Target.Id = 100;
	Target.Request.Group = TAG_Target;
	Target.Request.ViewerScope = EVBIndicatorViewerScope::OneViewer;
	Target.Request.Viewer = F.ViewerA;
	Target.Request.AnchorComponent = F.MakeAnchor(*this, Actors[6]);
	Target.Request.bSuppressed = true;
	Target.bPassesScreenPolicy = false;
	FVBIndicatorSelectionCandidate Boss = Candidates[2];
	Boss.Id = 101;
	Boss.Request.Group = TAG_Boss;
	Boss.bPassesScreenPolicy = false;
	// widget class 필드가 없다 - class 없는 BossBar 요청도 입력 계약이 똑같다.
	TArray<FVBIndicatorSelectionCandidate> WithClaims = Candidates;
	WithClaims.Add(Target);
	WithClaims.Add(Boss);
	const auto ClaimsA = VBIndicatorSelection::BuildClaims(WithClaims, Claiming, F.ViewerA);
	const auto ClaimsB = VBIndicatorSelection::BuildClaims(WithClaims, Claiming, F.ViewerB);
	FVBIndicatorSelectionParams Params;
	Params.Group = TAG_Nearby;
	Params.Radius = 1500.0f;
	Params.MaxCount = 2;
	Params.ExcludeClaimedBy.AddTag(TAG_Target);
	const auto Picked = VBIndicatorSelection::Select(WithClaims, Params, F.ViewerA, ClaimsA, IsDead);
	TArray<AActor*> PickedActors;
	for (uint64 Id : Picked)
	{
		PickedActors.Add(Actors[int32(Id - 1)]);
	}

	TestTrue(TEXT("Selection.LegacyPickNearby.SameOrderedAnswers"),
		PickedActors == VBEnemyBars::PickNearby(Legacy, 1500.0f, 2, Actors[6], nullptr));
	TestTrue(TEXT("Selection.LegacyPickNearby.ExpectedNearest"), Picked == TArray<uint64>({2, 3}));
	Params.ExcludeClaimedBy.AddTag(TAG_Boss);
	Params.MaxCount = 10;
	const auto BothExcluded = VBIndicatorSelection::Select(WithClaims, Params, F.ViewerA, ClaimsA, IsDead);
	PickedActors.Reset();
	for (uint64 Id : BothExcluded)
	{
		PickedActors.Add(Actors[int32(Id - 1)]);
	}

	TestTrue(TEXT("Selection.LegacyPickNearby.SecondExclusion"),
		PickedActors == VBEnemyBars::PickNearby(Legacy, 1500.0f, 10, Actors[6], Actors[2]));
	TestTrue(TEXT("Selection.ClaimIgnoresAnchorComponentIdentity"), !BothExcluded.Contains(7));
	TestTrue(TEXT("Selection.ClasslessOffscreenBossClaims"), !BothExcluded.Contains(3));
	TestTrue(TEXT("Selection.SuppressedUnboundTargetStillClaims"), !BothExcluded.Contains(7));
	const auto OtherViewer = VBIndicatorSelection::Select(WithClaims, Params, F.ViewerB, ClaimsB, IsDead);
	TestTrue(TEXT("Selection.ClaimsPerViewer"), OtherViewer.Contains(7) && !OtherViewer.Contains(3));
	TestFalse(TEXT("Selection.CannotBorrowOtherViewerClaims"),
		VBIndicatorSelection::IsClaimed(ClaimsA, F.ViewerB, Actors[6], Params.ExcludeClaimedBy));
	TArray<FVBIndicatorSelectionCandidate> Reordered = WithClaims;
	Swap(Reordered[0], Reordered.Last());
	const auto ReorderedClaims = VBIndicatorSelection::BuildClaims(Reordered, Claiming, F.ViewerA);
	TestTrue(TEXT("Selection.GroupOrderIndependent"),
		VBIndicatorSelection::Select(Reordered, Params, F.ViewerA, ReorderedClaims, IsDead) == BothExcluded);
	Params.MaxCount = 0;
	TestEqual(TEXT("Selection.ZeroMax"), VBIndicatorSelection::Select(Candidates, Params, F.ViewerA, ClaimsA, IsDead).Num(), 0);
	Params.MaxCount = -1;
	TestEqual(TEXT("Selection.NegativeMax"), VBIndicatorSelection::Select(Candidates, Params, F.ViewerA, ClaimsA, IsDead).Num(), 0);

	// 거리가 같으면 후보 저장 순서가 섞여도 등록 ID 로 가른다.
	TArray<FVBIndicatorSelectionCandidate> Equal = {Candidates[2], Candidates[1], Candidates[0]};
	for (auto& Candidate : Equal)
	{
		Candidate.DistanceSquared = FMath::Square(300.0f);
	}

	Params.ExcludeClaimedBy.Reset();
	Params.MaxCount = 3;
	const auto None = VBIndicatorSelection::BuildClaims(Candidates, {}, F.ViewerA);
	TestTrue(TEXT("Selection.DistanceTieRegistrationOrder"),
		VBIndicatorSelection::Select(Equal, Params, F.ViewerA, None, IsDead) == TArray<uint64>({1, 2, 3}));
	Equal[0].Request.Priority = 10;
	Equal[1].Request.Priority = 10;
	Equal[1].DistanceSquared = FMath::Square(900.0f);
	Params.SortOrder = EVBIndicatorSortOrder::PriorityThenRegistration;
	TestTrue(TEXT("Selection.PriorityThenRegistrationNotDistance"),
		VBIndicatorSelection::Select(Equal, Params, F.ViewerA, None, IsDead) == TArray<uint64>({2, 3, 1}));

	FVBIndicatorSelectionCandidate Boundary = Candidates[0];
	Boundary.DistanceSquared = FMath::Square(0.1f);
	Params.SortOrder = EVBIndicatorSortOrder::Nearest;
	Params.Radius = 0.1f;
	const TArray<FVBIndicatorSelectionCandidate> JustBoundary = {Boundary};
	TestEqual(TEXT("Selection.FloatRadiusBoundaryInclusive"),
		VBIndicatorSelection::Select(JustBoundary, Params, F.ViewerA, None, IsDead).Num(), 1);
	Boundary.DistanceSquared = 0.0;
	Params.Radius = -20;
	TestEqual(TEXT("Selection.NegativeRadiusClampsToZero"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 1);
	Boundary.DistanceSquared = 1.0;
	TestEqual(TEXT("Selection.ZeroRadiusNotUnlimited"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	Params.Radius = 1500;
	Boundary.DistanceSquared = std::numeric_limits<double>::quiet_NaN();
	TestEqual(TEXT("Selection.NaNDistanceRejected"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	Boundary = Candidates[0];
	Boundary.Request.bSuppressed = true;
	TestEqual(TEXT("Selection.SuppressedVisualRejected"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	Boundary.Request.bSuppressed = false;
	Boundary.Request.AnchorComponent.Reset();
	TestEqual(TEXT("Selection.NullSourceRejected"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	Boundary = Candidates[5];
	Params.bFilterDead = false;
	TestEqual(TEXT("Selection.DeadPredicateCanBeDisabled"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 1);
	Params.bFilterDead = true;
	TestEqual(TEXT("Selection.DeadPredicateFilters"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	Boundary = Candidates[0];
	Boundary.Request.Subject.Reset();
	int32 PredicateCalls = 0;
	TestEqual(TEXT("Selection.OptionalSubjectNeedsNoDeadQuery"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None,
			[&PredicateCalls](const AActor*) { ++PredicateCalls; return true; }).Num(), 1);
	TestEqual(TEXT("Selection.OptionalSubjectPredicateNotCalled"), PredicateCalls, 0);
	Boundary = Candidates[0];
	AActor* ExpiringSubject = F.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("Fixture.ExpiringSubject"), ExpiringSubject))
	{
		return false;
	}

	Boundary.Request.Subject = ExpiringSubject;
	F.World->DestroyActor(ExpiringSubject);
	TestEqual(TEXT("Selection.ExpiredSubjectIsNotUnsetSubject"),
		VBIndicatorSelection::Select(TArray<FVBIndicatorSelectionCandidate>{Boundary}, Params, F.ViewerA, None, IsDead).Num(), 0);
	const FGameplayTag ParentGroup = FGameplayTag::RequestGameplayTag(TEXT("Indicator.Group.Test.D01a"), false);
	if (!TestTrue(TEXT("Fixture.NativeParentTag"), ParentGroup.IsValid()))
	{
		return false;
	}

	FVBIndicatorSelectionParams ParentOnlyExclusion = Params;
	ParentOnlyExclusion.ExcludeClaimedBy.Reset();
	ParentOnlyExclusion.ExcludeClaimedBy.AddTag(ParentGroup);
	TestTrue(TEXT("Selection.ExclusionGroupsUseExactTags"),
		VBIndicatorSelection::Select(WithClaims, ParentOnlyExclusion, F.ViewerA, ClaimsA, IsDead).Contains(7));
	F.World->DestroyActor(F.ViewerA);
	TestFalse(TEXT("Selection.ExpiredScopedViewerNotAll"), VBIndicatorSelection::MatchesViewer(Target.Request, F.ViewerB));
	return true;
}
#endif
