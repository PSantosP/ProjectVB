// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBCharacter.h"
#include "Character/VBLocomotionIntentComponent.h"
#include "Character/VBSlideMovementComponent.h"
#include "CharacterCommon/VBBodyControlPolicy.h"
#include "CharacterCommon/VBDeathSteps.h"
#include "Animation/VBMotionWarpingNames.h"
#include "MotionWarpingComponent.h"
#include "Engine/World.h"
#include "Misc/ScopeExit.h"

// capsule 회전 방식은 movement component 가 매 tick 이동 전에 정한다 - sprint 는 이동 방향, strafe, aim, lock, combat 은 카메라를 따른다.
//   capsule 회전을 가진 기능이나 죽음 연출이 있으면 CMC 회전 flag 셋(root motion 회전 포함)을 모두 끈다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRotationModeRuleTest, "VB.Unit.Character.RotationMode.Rule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRotationModeRuleTest::RunTest(const FString& Parameters)
{
	using namespace VBBodyControlPolicy;
	auto Resolve = [](bool bSprint, bool bStrafe, bool bAim, bool bLock, bool bCombat, bool bOwned)
	{
		FRotationModeInputs In;
		In.bSprinting = bSprint; In.bWantsToStrafe = bStrafe; In.bWantsToAim = bAim;
		In.bLockedOn = bLock; In.bInCombat = bCombat; In.bCapsuleRotationOwned = bOwned;
		return ResolveRotationMode(In);
	};
	FRotationMode M = Resolve(false, false, false, false, false, false);
	TestTrue(TEXT("nothing wanted: turn to movement"), M.bOrientRotationToMovement && !M.bUseControllerDesiredRotation);
	TestTrue(TEXT("not owned: root-motion rotation on"), M.bAllowPhysicsRotationDuringAnimRootMotion);
	for (int32 Reason = 0; Reason < 4; ++Reason)
	{
		M = Resolve(false, Reason == 0, Reason == 1, Reason == 2, Reason == 3, false);
		TestTrue(FString::Printf(TEXT("strafe reason %d: follow the camera"), Reason), !M.bOrientRotationToMovement
			&& M.bUseControllerDesiredRotation);
		M = Resolve(true, Reason == 0, Reason == 1, Reason == 2, Reason == 3, false);
		TestTrue(FString::Printf(TEXT("sprint beats strafe reason %d"), Reason), M.bOrientRotationToMovement
			&& !M.bUseControllerDesiredRotation);
	}

	M = Resolve(false, true, false, false, false, true);
	TestTrue(TEXT("owned turns all three off (strafe)"), !M.bOrientRotationToMovement && !M.bUseControllerDesiredRotation
		&& !M.bAllowPhysicsRotationDuringAnimRootMotion);
	M = Resolve(true, false, false, false, false, true);
	TestTrue(TEXT("owned turns all three off (sprint)"), !M.bOrientRotationToMovement && !M.bUseControllerDesiredRotation
		&& !M.bAllowPhysicsRotationDuringAnimRootMotion);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBRotationModeTickTest, "VB.Unit.Character.RotationMode.Tick",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBRotationModeTickTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(true)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	AVBCharacter* Body = World->SpawnActor<AVBCharacter>();
	if (!TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	ON_SCOPE_EXIT { Body->Destroy(); };
	UCharacterMovementComponent* Move = Body->GetCharacterMovement();
	UVBLocomotionIntentComponent* Intent = UVBLocomotionIntentComponent::FindFor(Body);
	if (!TestNotNull(TEXT("intent owner"), Intent) || !TestNotNull(TEXT("movement"), Move))
	{
		return false;
	}

	// 아무도 부르지 않고 사실만 바뀌어도 다음 movement tick 이 정한다 (기본 strafe 바람 -> 카메라 따름).
	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("the movement tick decides the rotation mode (strafe wish follows the camera)"),
		!Move->bOrientRotationToMovement && Move->bUseControllerDesiredRotation);
	Intent->StartSprint();
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("sprint: the next tick turns to movement"), Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	Intent->StopSprint();

	// root motion 회전 flag 는 movement tick 만 쓴다 - capsule 주인이 없으면 켜고, attack warp target 이 쥐면 방향 flag 와 함께 끈다.
	Move->bAllowPhysicsRotationDuringAnimRootMotion = false;
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("free: the tick turns root-motion rotation on"), Move->bAllowPhysicsRotationDuringAnimRootMotion);
	UMotionWarpingComponent* Warping = Body->FindComponentByClass<UMotionWarpingComponent>();
	if (!TestNotNull(TEXT("motion warping"), Warping))
	{
		return false;
	}

	Warping->AddOrUpdateWarpTargetFromLocation(VBMotionWarpingNames::AttackTarget, Body->GetActorLocation() + FVector(200.0, 0.0, 0.0));
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("attack warp: the tick turns all three off"),
		!Move->bAllowPhysicsRotationDuringAnimRootMotion && !Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("warp removed: the tick turns root-motion rotation back on"), Move->bAllowPhysicsRotationDuringAnimRootMotion);

	// movement tick 을 멈추는 쪽(ragdoll)이 먼저 회전 방식을 정한다 - tick 이 꺼진 동안엔 아무도 안 정한다.
	UVBSlideMovementComponent* SlideMove = Cast<UVBSlideMovementComponent>(Move);
	if (!TestNotNull(TEXT("slide movement component"), SlideMove))
	{
		return false;
	}

	Move->bOrientRotationToMovement = true;
	Move->bUseControllerDesiredRotation = false;
	SlideMove->StopMovementTick();
	TestFalse(TEXT("stopping: the tick is off"), Move->IsComponentTickEnabled());
	TestTrue(TEXT("stopping: the rotation mode was decided first"), !Move->bOrientRotationToMovement
		&& Move->bUseControllerDesiredRotation);
	Move->SetComponentTickEnabled(true);

	// 죽음 연출은 모든 기계에서 이동 방향을 쥔다 - 둘을 한 번에 끄고 다음 tick 도 꺼 둔다.
	IVBDeathSteps* Steps = Body;
	Steps->PresentDeath(FVBDeathFacts());
	TestTrue(TEXT("death presented: both off at once"), !Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation);
	Move->TickComponent(1.0f / 60.0f, LEVELTICK_All, nullptr);
	TestTrue(TEXT("death presented: the next tick keeps all three off"),
		!Move->bOrientRotationToMovement && !Move->bUseControllerDesiredRotation && !Move->bAllowPhysicsRotationDuringAnimRootMotion);
	return true;
}
#endif
