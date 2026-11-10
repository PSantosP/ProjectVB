// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/VBApproachWarpDriver.h"
#include "Animation/AnimMontage.h"
#include "Animation/VBMotionWarpingNames.h"
#include "AnimNotifyState_MotionWarping.h"
#include "Character/VBCharacter.h"
#include "Components/CapsuleComponent.h"
#include "Components/SceneComponent.h"
#include "Engine/World.h"
#include "MotionWarpingComponent.h"
#include "Misc/ScopeExit.h"
#include "RootMotionModifier_SkewWarp.h"
#include "VBPlayerBodyTestTypes.h"
#include "UObject/StrongObjectPtr.h"

#if WITH_DEV_AUTOMATION_TESTS

// 접근 워프 드라이버: 목표 세우기(따라가기, 고정), 멈추는 거리에서 붙잡기, 되돌리기, 대상 자격 상실, 다른 쪽이 지운 목표, 워프 창 다시 세우기.
//   대조: 창 확인이 리셋을 무시하면 "section jump" 줄이, 붙잡기를 안 하면 "inside the stop distance" 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBApproachWarpDriverTest, "VB.Unit.MeleeApproach.WarpDriver",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBApproachWarpDriverTest::RunTest(const FString& Parameters)
{
	const auto Init = UWorld::InitializationValues().AllowAudioPlayback(false).CreatePhysicsScene(false)
		.CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> Package(World->GetPackage());
	ON_SCOPE_EXIT { World->DestroyWorld(false); Package->SetDirtyFlag(false); };
	World->InitializeActorsForPlay(FURL());

	FActorSpawnParameters Params;
	Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	AVBCharacter* Character = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator, Params);
	UMotionWarpingComponent* Warping = Character ? Character->FindComponentByClass<UMotionWarpingComponent>() : nullptr;
	if (!TestTrue(TEXT("character with motion warping"), Character && Warping))
	{
		return false;
	}

	auto SpawnTarget = [World](const FVector& Location)
	{
		AActor* Actor = World->SpawnActor<AActor>();
		USceneComponent* Root = NewObject<USceneComponent>(Actor);
		Actor->SetRootComponent(Root);
		Root->RegisterComponent();
		Root->SetWorldLocation(Location);
		return Root;
	};
	const FVector Owner = Character->GetActorLocation();
	UAnimMontage* Plain = NewObject<UAnimMontage>();
	auto AttackTarget = [Warping]() { return Warping->FindWarpTarget(VBMotionWarpingNames::AttackTarget); };

	// 목표 세우기
	FVBApproachWarpDriver Driver;
	TestTrue(TEXT("bind to a montage"), Driver.Bind(Warping, Plain, false));
	USceneComponent* Target = SpawnTarget(Owner + FVector(300.0, 0.0, 0.0));
	Driver.SetGoal(*Warping, *Character, *Target, 100.0f, true);
	const FMotionWarpingTarget* Follow = AttackTarget();
	if (TestNotNull(TEXT("follow goal written"), Follow))
	{
		TestTrue(TEXT("follow goal follows the target"), Follow->bFollowComponent);
		TestEqual(TEXT("follow goal is the stop distance in front of the target"), Follow->GetLocation().X, Owner.X + 200.0, 1.0);
	}

	Driver.SetGoal(*Warping, *Character, *Target, 100.0f, false);
	const FMotionWarpingTarget* Fixed = AttackTarget();
	if (TestNotNull(TEXT("fixed goal written"), Fixed))
	{
		TestFalse(TEXT("fixed goal does not follow"), Fixed->bFollowComponent);
		TestEqual(TEXT("fixed goal X"), Fixed->Location.X, Owner.X + 200.0, 1.0);
		TestEqual(TEXT("fixed goal faces the target"), Fixed->Rotation.Yaw, 0.0, 1.0);
	}

	// 멈추는 거리에서 붙잡기, 되돌리기 (따라가는 목표, 멈춤 100, 앞섬 2)
	Driver.SetGoal(*Warping, *Character, *Target, 100.0f, true);
	Target->SetWorldLocation(Owner + FVector(60.0, 0.0, 0.0));
	FVBWarpHoldResult Result = Driver.Hold(*Character, 2.0f);
	TestTrue(TEXT("target inside the stop distance: hold starts"), Result.Event == EVBWarpHoldEvent::HoldStarted);
	TestEqual(TEXT("hold reports the distance"), Result.Distance, 60.0f, 1.0f);
	if (const FMotionWarpingTarget* Held = AttackTarget())
	{
		TestFalse(TEXT("held goal is a fixed point"), Held->bFollowComponent);
		TestEqual(TEXT("held point is MinLead toward the target"), Held->Location.X, Owner.X + 2.0, 0.5);
	}

	TestTrue(TEXT("still inside: no new event"), Driver.Hold(*Character, 2.0f).Event == EVBWarpHoldEvent::None);
	Target->SetWorldLocation(Owner + FVector(400.0, 0.0, 0.0));
	Result = Driver.Hold(*Character, 2.0f);
	TestTrue(TEXT("target moved away: restored"), Result.Event == EVBWarpHoldEvent::Restored);
	const FMotionWarpingTarget* Restored = AttackTarget();
	TestTrue(TEXT("restored goal follows again"), Restored && Restored->bFollowComponent);

	// 다른 쪽이 지운 목표는 잊고 되살리지 않는다
	Warping->RemoveWarpTarget(VBMotionWarpingNames::AttackTarget);
	TestTrue(TEXT("removed elsewhere: forgot"), Driver.Hold(*Character, 2.0f).Event == EVBWarpHoldEvent::Forgot);
	TestNull(TEXT("removed elsewhere: not re-added"), AttackTarget());
	TestTrue(TEXT("after forgetting: nothing"), Driver.Hold(*Character, 2.0f).Event == EVBWarpHoldEvent::None);
	TestNull(TEXT("after forgetting: still not re-added"), AttackTarget());

	// 대상이 남아 있지만 자격을 잃으면 Dropped - 부르는 쪽이 이유를 남기고 끊는다(파괴는 능력의 파괴 구독이 받는다)
	AVBTestLockTarget* Lockable = World->SpawnActor<AVBTestLockTarget>();
	USceneComponent* LockableRoot = Lockable ? NewObject<USceneComponent>(Lockable) : nullptr;
	if (!TestNotNull(TEXT("targetable actor"), LockableRoot))
	{
		return false;
	}

	Lockable->SetRootComponent(LockableRoot);
	LockableRoot->RegisterComponent();
	LockableRoot->SetWorldLocation(Owner + FVector(300.0, 0.0, 0.0));
	Driver.SetGoal(*Warping, *Character, *LockableRoot, 100.0f, true);
	TestTrue(TEXT("targetable: kept"), Driver.Hold(*Character, 2.0f).Event == EVBWarpHoldEvent::None);
	Lockable->bTargetable = false;
	TestTrue(TEXT("no longer targetable: dropped"), Driver.Hold(*Character, 2.0f).Event == EVBWarpHoldEvent::Dropped);
	TestNotNull(TEXT("dropped: the caller clears, not the driver"), AttackTarget());
	Driver.Clear(Warping);
	TestNull(TEXT("clear removes the goal"), AttackTarget());

	// 워프 창 다시 세우기 - AttackTarget 창 0.2, 0.6, 다른 대상 창 하나
	UAnimMontage* Windows = NewObject<UAnimMontage>();
	auto AddWarp = [Windows](FName TargetName, float Time)
	{
		UAnimNotifyState_MotionWarping* State = NewObject<UAnimNotifyState_MotionWarping>(Windows);
		URootMotionModifier_SkewWarp* Modifier = NewObject<URootMotionModifier_SkewWarp>(State);
		Modifier->WarpTargetName = TargetName;
		State->RootMotionModifier = Modifier;
		FAnimNotifyEvent& Event = Windows->Notifies.AddDefaulted_GetRef();
		Event.NotifyStateClass = State;
		Event.SetTime(Time);
		Event.SetDuration(0.1f);
	};
	AddWarp(VBMotionWarpingNames::AttackTarget, 0.2f);
	AddWarp(TEXT("SomethingElse"), 0.4f);
	AddWarp(VBMotionWarpingNames::AttackTarget, 0.6f);
	TestTrue(TEXT("bind with windows"), Driver.Bind(Warping, Windows, true));
	float Start = -1.0f;
	TestFalse(TEXT("first check after bind only records the position"), Driver.PassedWindowStart(0.1f, Start));
	TestTrue(TEXT("crossing 0.2 rearms"), Driver.PassedWindowStart(0.3f, Start));
	TestEqual(TEXT("crossed start"), Start, 0.2f, 0.001f);
	TestFalse(TEXT("other target's window does not rearm"), Driver.PassedWindowStart(0.5f, Start));
	TestTrue(TEXT("crossing 0.6 rearms"), Driver.PassedWindowStart(0.7f, Start));
	TestEqual(TEXT("crossed second start"), Start, 0.6f, 0.001f);
	TestTrue(TEXT("rebind with windows"), Driver.Bind(Warping, Windows, true));
	TestFalse(TEXT("rebind: first check records"), Driver.PassedWindowStart(0.1f, Start));
	TestFalse(TEXT("before the first start"), Driver.PassedWindowStart(0.15f, Start));
	Driver.ResetWindowCheck();
	TestFalse(TEXT("section jump past both starts is not read as passed"), Driver.PassedWindowStart(0.7f, Start));
	TestTrue(TEXT("without tracking: no windows"), Driver.Bind(Warping, Windows, false));
	Driver.PassedWindowStart(0.1f, Start);
	TestFalse(TEXT("without tracking: never rearms"), Driver.PassedWindowStart(0.7f, Start));
	TestFalse(TEXT("no montage: not bound"), Driver.Bind(Warping, nullptr, true));
	TestNull(TEXT("no montage: nothing to unbind"), Driver.Unbind());

	// 캡슐 합
	const float SelfRadius = Character->GetCapsuleComponent()->GetScaledCapsuleRadius();
	TestEqual(TEXT("separation from a target without a capsule"),
		FVBApproachWarpDriver::MinSeparation(Character, SpawnTarget(FVector::ZeroVector)), SelfRadius, 0.01f);
	TestEqual(TEXT("separation without self"), FVBApproachWarpDriver::MinSeparation(nullptr, Target), 0.0f, 0.01f);
	Character->Destroy();
	return true;
}

#endif
