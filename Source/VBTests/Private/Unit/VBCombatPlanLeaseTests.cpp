#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AI/VBCombatCoordinatorSubsystem.h"
#include "AI/VBEnemyCombatComponent.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

class FVBCombatLeaseReproAccess
{
public:
    static void SeedPlan(UVBEnemyCombatComponent& Combat)
    {
        Combat.PlannedVariantIndex = 0;
        Combat.PlannedStartDistance = 42.0f;
    }

    static int32 GetPlan(const UVBEnemyCombatComponent& Combat) { return Combat.PlannedVariantIndex; }
    static bool RevokeLeaseOnly(UVBCombatCoordinatorSubsystem& Coordinator, UVBEnemyCombatComponent& Combat)
    {
        FVBCombatGroup* Group = Coordinator.FindGroupForMemberMutable(&Combat);
        FVBCombatMemberState* State = Group ? Group->Members.Find(&Combat) : nullptr;
        if (!State)
        {
            return false;
        }

        State->ClearLease();
        return true;
    }
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatPlanLeaseReproTest, "VB.Unit.Combat.PlannedVariantLease",
    EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatPlanLeaseReproTest::RunTest(const FString& Parameters)
{
    const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false)
        .CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false)
        .ShouldSimulatePhysics(false).SetTransactional(false);
    UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true,
        ERHIFeatureLevel::Num, &Init);
    if (!TestNotNull(TEXT("isolated combat world"), World))
    {
        return false;
    }

    UPackage* Package = World->GetPackage();
    ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
    AActor* Owner = World->SpawnActor<AActor>();
    AActor* Target = World->SpawnActor<AActor>();
    if (!TestNotNull(TEXT("combat owner"), Owner) || !TestNotNull(TEXT("combat target"), Target))
    {
        return false;
    }

    UVBEnemyCombatComponent* Combat = NewObject<UVBEnemyCombatComponent>(Owner);
    Combat->RegisterComponent();
    Combat->SetCombatTarget(Target);
    UVBCombatCoordinatorSubsystem* Coordinator = World->GetSubsystem<UVBCombatCoordinatorSubsystem>();
    if (!TestNotNull(TEXT("real coordinator"), Coordinator))
    {
        return false;
    }

    if (!TestFalse(TEXT("no lease before explicit public acquire"), Combat->HasAttackToken()))
    {
        return false;
    }

    if (!TestTrue(TEXT("public acquire succeeds"), Coordinator->TryAcquireAttackToken(Combat, 1)))
    {
        return false;
    }

    if (!TestTrue(TEXT("positive control really holds a token"), Combat->HasAttackToken()))
    {
        return false;
    }

    FVBCombatLeaseReproAccess::SeedPlan(*Combat);
    TestEqual(TEXT("owned lease consumes its planned variant"), Combat->ConsumePlannedVariantIndex(), 0);
    TestEqual(TEXT("owned lease consumes the index only once"), FVBCombatLeaseReproAccess::GetPlan(*Combat), INDEX_NONE);
    TestEqual(TEXT("owned lease preserves swing start distance"), Combat->GetAttackStartDistance(), 42.0f);
    FVBCombatLeaseReproAccess::SeedPlan(*Combat);
    if (!TestTrue(TEXT("real group lease is cleared without touching the plan"),
        FVBCombatLeaseReproAccess::RevokeLeaseOnly(*Coordinator, *Combat)))
    {
        return false;
    }

    if (!TestFalse(TEXT("revoked lease really no longer owns a token"), Combat->HasAttackToken()))
    {
        return false;
    }

    if (!TestEqual(TEXT("plan remains after lease-only revocation"), FVBCombatLeaseReproAccess::GetPlan(*Combat), 0))
    {
        return false;
    }

    TestEqual(TEXT("revoked lease never consumes stale planned variant"), Combat->ConsumePlannedVariantIndex(), INDEX_NONE);
    TestEqual(TEXT("stale plan index is discarded"), FVBCombatLeaseReproAccess::GetPlan(*Combat), INDEX_NONE);
    Combat->LeaveCombat();
    return true;
}
#endif
