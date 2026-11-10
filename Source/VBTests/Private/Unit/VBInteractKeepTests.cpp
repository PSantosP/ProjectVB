#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBInteractableConfig.h"
#include "Interaction/VBInteractorComponent.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"

// 하고 있는 상호작용을 이어 갈지, 움직이면 취소할지의 단위 시험.

// 고르기(CanReach)는 카메라 방향을 보고, 이어 가기(CanKeep)는 안 본다. 거리는 둘 다 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractKeepTest, "VB.Unit.Interaction.KeepIgnoresFacing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractKeepTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// 컨트롤러 없는 폰의 컨트롤 회전 = 0 -> 카메라가 +X 를 본다.
	APawn* Pawn = World->SpawnActor<APawn>();
	AActor* Prop = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("pawn"), Pawn) || !TestNotNull(TEXT("prop"), Prop))
	{
		return false;
	}

	UVBInteractorComponent* Interactor = NewObject<UVBInteractorComponent>(Pawn);
	Pawn->AddInstanceComponent(Interactor);
	Interactor->RegisterComponent();
	UVBInteractableComponent* Target = NewObject<UVBInteractableComponent>(Prop);
	Prop->SetRootComponent(Target);
	Prop->AddInstanceComponent(Target);
	Target->RegisterComponent();
	const float Range = Target->GetConfig()->InteractRange;
	const float MaxYaw = Target->GetConfig()->ReachRules.MaxYawAngle;
	if (!TestTrue(TEXT("default config: facing rule narrower than behind"), MaxYaw < 170.0f && Range > 100.0f))
	{
		return false;
	}

	Prop->SetActorLocation(FVector(100.0f, 0.0f, 0.0f));
	TestTrue(TEXT("in front: can pick"), Interactor->CanReach(Target));
	TestTrue(TEXT("in front: can keep"), Interactor->CanKeep(Target));

	// 줍는 도중 카메라를 달릴 쪽으로 돌린 것과 같다 - 대상이 카메라 뒤.
	Prop->SetActorLocation(FVector(-100.0f, 0.0f, 0.0f));
	TestFalse(TEXT("behind the camera: cannot pick"), Interactor->CanReach(Target));
	TestTrue(TEXT("behind the camera: an interaction in progress still keeps"), Interactor->CanKeep(Target));

	Prop->SetActorLocation(FVector(-(Range + 50.0f), 0.0f, 0.0f));
	TestFalse(TEXT("out of range: cannot keep"), Interactor->CanKeep(Target));
	Prop->SetActorLocation(FVector(100.0f, 0.0f, Target->GetConfig()->ReachRules.MaxHeightDifference + 50.0f));
	TestFalse(TEXT("height rule still applies to keep"), Interactor->CanKeep(Target));
	return true;
}

// 달려와서 누르면 시작 때 이미 이동 입력이 있다 - 그것으로는 취소하지 않고, 한 번 멎은 뒤 다시 움직일 때만 취소한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractMoveCancelGateTest, "VB.Unit.Interaction.MoveCancelGate",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractMoveCancelGateTest::RunTest(const FString& Parameters)
{
	FVBMoveCancelGate Gate;
	TestFalse(TEXT("input held from the approach does not cancel"), Gate.Step(true));
	TestFalse(TEXT("still held: no cancel"), Gate.Step(true));
	TestFalse(TEXT("released: no cancel"), Gate.Step(false));
	TestTrue(TEXT("pressed again: cancel"), Gate.Step(true));
	Gate.Reset();
	TestFalse(TEXT("after reset the held input is again ignored"), Gate.Step(true));

	TestTrue(TEXT("default config cancels on move (doors, levers)"), GetDefault<UVBInteractableConfig>()->bCancelOnMove);
	return true;
}

// interactor 가 capsule 방향을 쥔 동안 이동 입력은 movement component 에 닿지 않는다(가속 0).
// 그래도 raw stick 은 쳐야 하고, 놓은 스틱은 치지 않아야 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractMoveInputDuringFacingTest, "VB.Unit.Interaction.MoveInputDuringFacing",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractMoveInputDuringFacingTest::RunTest(const FString& Parameters)
{
	const FVector NoAcceleration = FVector::ZeroVector;
	const FVector2D Pushed(0.0, 1.0);
	const FVector2D Released = FVector2D::ZeroVector;
	TestTrue(TEXT("facing owned (no acceleration) + stick pushed counts as move input"),
		FVBMoveCancelGate::HasMoveInput(NoAcceleration, Pushed));
	TestFalse(TEXT("facing owned + stick released is no move input"), FVBMoveCancelGate::HasMoveInput(NoAcceleration, Released));
	TestTrue(TEXT("normal walking (acceleration, stick) still counts"), FVBMoveCancelGate::HasMoveInput(FVector(0.0, 600.0, 0.0), Pushed));

	// 몸 동작 위의 gate: 접근부터 쥠 -> 놓음 -> 방향을 쥔 동안 다시 밂 -> 취소. 방향을 쥔 동안 놓기만 하면 취소하지 않는다.
	FVBMoveCancelGate Gate;
	TestFalse(TEXT("stick held from the approach does not cancel"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Pushed)));
	TestFalse(TEXT("release during the body motion does not cancel"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Released)));
	TestTrue(TEXT("new push during the body motion cancels"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Pushed)));

	// 접촉(VBGA_Interact commit) 뒤 gate 는 처음부터 - 접촉 내내 쥔 스틱은 회복을 끊지 않고, 놓았다 다시 밀어야 끊는다.
	Gate.Reset();
	TestFalse(TEXT("after contact: stick held through the contact does not cut"),
		Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Pushed)));
	TestFalse(TEXT("after contact: release does not cut"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Released)));
	TestTrue(TEXT("after contact: new push cuts the recovery"), Gate.Step(FVBMoveCancelGate::HasMoveInput(NoAcceleration, Pushed)));
	return true;
}

// 실제 자산: 줍기는 끝까지(이동 잠금), 문, 레버는 움직이면 취소.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractMovePolicyAssetsTest, "VB.Unit.Interaction.MovePolicyAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractMovePolicyAssetsTest::RunTest(const FString& Parameters)
{
	auto Load = [this](const TCHAR* Path) -> const UVBInteractableConfig*
	{
		const UVBInteractableConfig* Config = LoadObject<UVBInteractableConfig>(nullptr, Path);
		TestNotNull(FString::Printf(TEXT("%s loads"), Path), Config);
		return Config;
	};
	const UVBInteractableConfig* Pickup = Load(TEXT("/Game/VB/Data/Interaction/DA_Interactable_Pickup.DA_Interactable_Pickup"));
	const UVBInteractableConfig* Door = Load(TEXT("/Game/VB/Data/Interaction/DA_Interactable_Door.DA_Interactable_Door"));
	const UVBInteractableConfig* Lever = Load(TEXT("/Game/VB/Data/Interaction/DA_Interactable_Lever.DA_Interactable_Lever"));
	if (!Pickup || !Door || !Lever)
	{
		return false;
	}

	TestFalse(TEXT("pickup keeps going when you move (move locked until contact)"), Pickup->bCancelOnMove);
	TestTrue(TEXT("door cancels when you move before contact"), Door->bCancelOnMove);
	TestTrue(TEXT("lever hold cancels when you move"), Lever->bCancelOnMove);
	// 싸우는 중 즉시 하기: 줍기만 켠다. 문, 레버는 user 가 따로 정할 때까지 몸 동작 그대로.
	TestTrue(TEXT("pickup is instant while engaged"), Pickup->bInstantWhileEngaged);
	TestFalse(TEXT("door keeps its body motion while engaged"), Door->bInstantWhileEngaged);
	TestFalse(TEXT("lever keeps its body motion while engaged"), Lever->bInstantWhileEngaged);
	return true;
}

// 몸 동작을 건너뛰는 조건: Tap + 플래그 + 싸우는 중, 셋이 다 맞을 때만.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractSkipMotionTest, "VB.Unit.Interaction.SkipMotionWhileEngaged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractSkipMotionTest::RunTest(const FString& Parameters)
{
	UVBInteractableConfig* Config = NewObject<UVBInteractableConfig>(GetTransientPackage());
	TestFalse(TEXT("default: flag off"), Config->bInstantWhileEngaged);
	TestTrue(TEXT("default: post-contact blend-out keeps the old 0.2 s"), FMath::IsNearlyEqual(Config->PostContactBlendOutSeconds, 0.2f));
	TestFalse(TEXT("flag off + engaged -> body motion"), Config->ShouldSkipMotion(true));
	Config->bInstantWhileEngaged = true;
	Config->Mode = EVBInteractMode::Tap;
	TestTrue(TEXT("Tap + flag + engaged -> instant"), Config->ShouldSkipMotion(true));
	TestFalse(TEXT("Tap + flag + not engaged -> body motion"), Config->ShouldSkipMotion(false));
	Config->Mode = EVBInteractMode::Hold;
	TestFalse(TEXT("Hold is never instant (the hold time is the interaction)"), Config->ShouldSkipMotion(true));
	Config->Mode = EVBInteractMode::Push;
	TestFalse(TEXT("Push is never instant"), Config->ShouldSkipMotion(true));
	return true;
}
#endif
