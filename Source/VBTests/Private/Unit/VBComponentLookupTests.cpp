// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBCharacter.h"
#include "Character/VBPlayerRagdollComponent.h"
#include "Character/VBTargetLockComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Interaction/VBGASPSmartObjectComponent.h"
#include "Interaction/VBInteractorComponent.h"
#include "Misc/ScopeExit.h"
#include "Traversal/VBTraversalComponent.h"
#include "Types/VBWeaponStateTypes.h"

namespace VBComponentLookupTest
{
	UWorld* MakeWorld()
	{
		const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
			.CreateNavigation(false).CreateAISystem(false);
		UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
		if (World)
		{
			World->InitializeActorsForPlay(FURL());
		}

		return World;
	}
}

// 기능 component 는 몸의 getter 가 아니라 각 component 의 FindFor 로 찾는다. 플레이어 몸은 생성자에서 만들어 FindFor 가 돌려주고,
//   우리 몸이 아닌 actor(적, 그냥 ACharacter)는 null 이다 - 호출자는 이것을 "이 actor 는 그것을 안 한다" 로 읽는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComponentLookupFindForTest, "VB.Unit.Character.ComponentLookup.FindFor",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBComponentLookupFindForTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBComponentLookupTest::MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	ACharacter* Other = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("body"), Body) || !TestNotNull(TEXT("other character"), Other))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); Other->Destroy(); };

	const UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Body);
	const UVBTargetLockComponent* Lock = UVBTargetLockComponent::FindFor(Body);
	TestTrue(TEXT("the body's weapon state is found and is the body's own"), Weapons && Weapons->GetOwner() == Body);
	TestTrue(TEXT("the body's lock-on is found and is the body's own"), Lock && Lock->GetOwner() == Body);
	TestNull(TEXT("an actor that is not our body has no weapon state"), UVBWeaponStateComponent::FindFor(Other));
	TestNull(TEXT("an actor that is not our body has no lock-on"), UVBTargetLockComponent::FindFor(Other));
	const auto Owned = [Body](const UActorComponent* Component) { return Component && Component->GetOwner() == Body; };
	TestTrue(TEXT("the body's traversal is found"), Owned(UVBTraversalComponent::FindFor(Body)));
	TestTrue(TEXT("the body's interactor is found"), Owned(UVBInteractorComponent::FindFor(Body)));
	TestTrue(TEXT("the body's player ragdoll is found"), Owned(UVBPlayerRagdollComponent::FindFor(Body)));
	TestTrue(TEXT("the body's smart object component is found"), Owned(UVBGASPSmartObjectComponent::FindFor(Body)));
	TestNull(TEXT("not our body: no traversal"), UVBTraversalComponent::FindFor(Other));
	TestNull(TEXT("not our body: no interactor"), UVBInteractorComponent::FindFor(Other));
	TestNull(TEXT("not our body: no player ragdoll"), UVBPlayerRagdollComponent::FindFor(Other));
	TestNull(TEXT("not our body: no smart object component"), UVBGASPSmartObjectComponent::FindFor(Other));
	TestNull(TEXT("no actor: no weapon state"), UVBWeaponStateComponent::FindFor(nullptr));
	TestNull(TEXT("no actor: no lock-on"), UVBTargetLockComponent::FindFor(nullptr));
	return true;
}

// EnterCombat 은 authority 를 스스로 본다 - 호출자(회피, 치료, 근접, 잠금)는 서버와 소유 클라이언트에서 돌지만
//   상태는 서버 호출만 바꾼다. authority 없는 호출은 아무것도 안 바꾼다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBComponentLookupEnterCombatAuthorityTest, "VB.Unit.Character.ComponentLookup.EnterCombatAuthority",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBComponentLookupEnterCombatAuthorityTest::RunTest(const FString& Parameters)
{
	UWorld* World = VBComponentLookupTest::MakeWorld();
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); };
	// EnterCombat 은 BeginPlay 에서 잡는 owner 도 필요하다 - 없으면 두 줄이 엉뚱한 이유로 통과한다
	Body->DispatchBeginPlay();
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Body);
	if (!TestNotNull(TEXT("weapon state"), Weapons))
	{
		return false;
	}

	if (!TestEqual(TEXT("precondition: starts out of combat"), Weapons->GetLocomotionState(), EVBLocomotionState::Unarmed))
	{
		return false;
	}

	Body->SetRole(ROLE_SimulatedProxy);
	Weapons->EnterCombat();
	TestEqual(TEXT("without authority: still out of combat"), Weapons->GetLocomotionState(), EVBLocomotionState::Unarmed);

	Body->SetRole(ROLE_Authority);
	Weapons->EnterCombat();
	TestEqual(TEXT("with authority: in combat, weapon not drawn"), Weapons->GetLocomotionState(), EVBLocomotionState::CombatSheathed);
	return true;
}
#endif
