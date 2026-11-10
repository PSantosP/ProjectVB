// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "GameplayTagContainer.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBPhysicalDoor.h"
#include "Narrative/Story/VBAreaTrigger.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "TimerManager.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Unit/VBPhysicalDoorTestAccess.h"

// 닿으면 멈추는 문. 물리 장면이 있는 격리 월드에서 문의 틱을 직접 부르고,
//   시간은 움직임의 시작 시각을 과거로 돌려 흘린다(월드를 돌리지 않는다).
//   덮지 않는 것: 실제 플레이어가 미는 느낌, 클라의 보간 - PIE 가 덮는다.

namespace VBPhysicalDoorTest
{
	// 경첩이 원점, 문짝 120(길이 X) x 4(두께 Y) x 200(높이 Z). 90도로 연다(+Y 쪽으로 돈다).
	struct FDoorWorld
	{
		FVBIsolatedGameWorld Isolated;
		AVBPhysicalDoor* Door = nullptr;
		TArray<TPair<uint32, EVBDoorMoveResult>> Results;

		// 월드가 끝날 때(EndPlay) 문이 보내는 Cancelled 가 이미 사라진 Results 에 쓰지 않게 먼저 끊는다.
		~FDoorWorld()
		{
			if (Door)
			{
				Door->OnDoorMoveFinished.Clear();
			}
		}

		bool Init(FAutomationTestBase& Test, EVBDoorMotion Motion = EVBDoorMotion::Swing, bool bWithMesh = true,
			const FTransform& Placement = FTransform::Identity, const FVector& HingeOffset = FVector::ZeroVector, float OpenAngle = 90.0f)
		{
			if (!SpawnDoor(Test, Motion, bWithMesh, Placement, HingeOffset, OpenAngle))
			{
				return false;
			}

			Door->DispatchBeginPlay();
			return true;
		}

		// 시작 전까지만 - 이야기 시험이 BeginPlay 전에 사실 저장소, 배치 값을 넣는다.
		bool SpawnDoor(FAutomationTestBase& Test, EVBDoorMotion Motion = EVBDoorMotion::Swing, bool bWithMesh = true,
			const FTransform& Placement = FTransform::Identity, const FVector& HingeOffset = FVector::ZeroVector, float OpenAngle = 90.0f)
		{
			if (!Isolated.Init(Test, /*bPhysicsScene=*/true)) return false;
			FActorSpawnParameters Spawn;
			Spawn.bDeferConstruction = true;
			Door = Isolated.World->SpawnActor<AVBPhysicalDoor>(Placement.GetLocation(), Placement.Rotator(), Spawn);
			if (!Test.TestNotNull(TEXT("door"), Door))
			{
				return false;
			}

			UStaticMeshComponent* Leaf = Door->FindComponentByClass<UStaticMeshComponent>();
			UStaticMesh* Cube = LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube"));
			if (!Test.TestNotNull(TEXT("leaf"), Leaf) || !Test.TestNotNull(TEXT("engine cube"), Cube))
			{
				return false;
			}

			if (bWithMesh)
			{
				Leaf->SetStaticMesh(Cube);
			}

			Leaf->SetRelativeLocation(FVector(60.0, 0.0, 100.0));
			Leaf->SetRelativeScale3D(FVector(1.2, 0.04, 2.0));
			if (const FObjectProperty* HingeProperty = FindFProperty<FObjectProperty>(AVBPhysicalDoor::StaticClass(), TEXT("Hinge")))
			{
				Cast<USceneComponent>(HingeProperty->GetObjectPropertyValue_InContainer(Door))->SetRelativeLocation(HingeOffset);
			}

			if (FFloatProperty* Angle = FindFProperty<FFloatProperty>(AVBPhysicalDoor::StaticClass(), TEXT("OpenAngle")))
			{
				Angle->SetPropertyValue_InContainer(Door, OpenAngle);
			}

			Set(TEXT("Motion"), static_cast<uint8>(Motion));
			Door->FinishSpawning(Placement);
			Door->OnDoorMoveFinished.AddLambda([this](uint32 Serial, EVBDoorMoveResult Result) { Results.Emplace(Serial, Result); });
			return true;
		}

		void Set(const TCHAR* Name, uint8 Value) const
		{
			if (FByteProperty* Byte = FindFProperty<FByteProperty>(AVBPhysicalDoor::StaticClass(), Name))
			{
				Byte->SetPropertyValue_InContainer(Door, Value);
			}
			else if (FEnumProperty* Enum = FindFProperty<FEnumProperty>(AVBPhysicalDoor::StaticClass(), Name))
			{
				Enum->GetUnderlyingProperty()->SetIntPropertyValue(Enum->ContainerPtrToValuePtr<void>(Door), static_cast<int64>(Value));
			}
		}

		AActor* SpawnBox(const FVector& Center, const FVector& Extent, bool bBlocking) const
		{
			AActor* Actor = Isolated.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->AddInstanceComponent(Box);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(Extent);
			if (bBlocking)
			{
				Box->SetCollisionObjectType(ECC_WorldDynamic);
				Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
				Box->SetCollisionResponseToAllChannels(ECR_Block);
			}
			else
			{
				// 영역 트리거와 같은 모양: 질의만, Pawn 겹침 말고는 모두 무시.
				Box->SetCollisionObjectType(ECC_WorldDynamic);
				Box->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
				Box->SetCollisionResponseToAllChannels(ECR_Ignore);
				Box->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
			}

			Actor->SetActorLocation(Center);
			Box->RegisterComponent();
			return Actor;
		}

		int32 Count(uint32 Serial, EVBDoorMoveResult Result) const
		{
			return Results.FilterByPredicate([Serial, Result](const TPair<uint32, EVBDoorMoveResult>& R) { return R.Key == Serial && R.Value == Result; }).Num();
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorSweptBoxTest, "VB.Unit.Interaction.PhysicalDoor.SweptBox",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorSweptBoxTest::RunTest(const FString& Parameters)
{
	// 문짝 단면 120 x 4, 경첩 원점. 0 -> 3 도 한 걸음을 담는 상자.
	FVector Center, Extent;
	float Mid = 0.0f;
	AVBPhysicalDoor::SweptSwingBox(FVector(60.0, 0.0, 0.0), FVector(60.0, 2.0, 100.0), 0.0f, 3.0f, Center, Extent, Mid);
	TestTrue(TEXT("mid angle"), FMath::IsNearlyEqual(Mid, 1.5f));
	const FTransform MidFrame(FQuat(FVector::UpVector, FMath::DegreesToRadians(Mid)));
	auto Inside = [&](const FVector& HingePoint)
	{
		const FVector Local = MidFrame.InverseTransformPosition(HingePoint) - Center;
		return FMath::Abs(Local.X) <= Extent.X + 1e-6 && FMath::Abs(Local.Y) <= Extent.Y + 1e-6 && FMath::Abs(Local.Z) <= Extent.Z + 1e-6;
	};
	// R1 반례: 끝 자세 둘(0, 3 도)에서는 안 닿고 1.5 도에서만 닿는 1cm 장애물.
	TestTrue(TEXT("a blocker touched only between the two end poses is inside the box"), Inside(FVector(114.9606, 3.01035, 0.0)));
	// R2 반례: 돌아간 두께 모서리는 반지름 쪽으로도 나간다 - 두 축 모두 담아야 한다.
	bool bAllInside = true;
	for (int32 Step = 0; Step <= 30; ++Step)
	{
		const FQuat Rot(FVector::UpVector, FMath::DegreesToRadians(3.0 * Step / 30.0));
		for (const FVector& Corner : {FVector(120.0, 2.0, 0.0), FVector(120.0, -2.0, 0.0), FVector(0.0, 2.0, 0.0), FVector(0.0, -2.0, 0.0)})
		{
			bAllInside &= Inside(Rot.RotateVector(Corner));
		}
	}

	TestTrue(TEXT("every rotated leaf corner in the interval is inside the box (both axes)"), bAllInside);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorSwingTest, "VB.Unit.Interaction.PhysicalDoor.SwingStopsOnContact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorSwingTest::RunTest(const FString& Parameters)
{
	VBPhysicalDoorTest::FDoorWorld F;
	if (!F.Init(*this))
	{
		return false;
	}

	if (!TestFalse(TEXT("valid door has no fault"), F.Door->HasFault()))
	{
		AddInfo(F.Door->GetFaultReason());
		return false;
	}

	const uint32 Open = F.Door->RequestMove(true);
	TestTrue(TEXT("open accepted"), Open != 0);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 0.5);
	const float Part = FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door);
	TestTrue(TEXT("half a second in: part way"), Part > 0.0f && Part < 1.0f);
	TestFalse(TEXT("not open before it reaches the end"), F.Door->IsOpen());
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestEqual(TEXT("open reached once"), F.Count(Open, EVBDoorMoveResult::Reached), 1);
	TestTrue(TEXT("open now (last reached end)"), F.Door->IsOpen());

	// 닫히는 길 한가운데(45 도, 반지름 100)에 막는 상자.
	AActor* Blocker = F.SpawnBox(FVector(70.7, 70.7, 100.0), FVector(5.0), /*bBlocking=*/true);
	const uint32 Close = F.Door->RequestMove(false);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestEqual(TEXT("close blocked once"), F.Count(Close, EVBDoorMoveResult::Blocked), 1);
	// 같은 판정(걸음 상자, 여유 1cm)을 따로 계산한 멈춤 자리는 알파 0.5597 - 상자가 틀리거나 걸음이 커지면 여기서 벗어난다.
	const float Stop = FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door);
	TestTrue(TEXT("stopped just before the blocker"), F.Door->IsBlocked() && Stop > 0.54f && Stop < 0.58f);
	TestTrue(TEXT("saved state stays the last reached end (open)"), F.Door->IsOpen());
	TestEqual(TEXT("no reached for the blocked close"), F.Count(Close, EVBDoorMoveResult::Reached), 0);

	// 비우고 다시 닫으면 닫힌다.
	Blocker->Destroy();
	const uint32 Retry = F.Door->RequestMove(false);
	TestTrue(TEXT("retry gets a new serial"), Retry != 0 && Retry != Close);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestEqual(TEXT("retry reached"), F.Count(Retry, EVBDoorMoveResult::Reached), 1);
	TestFalse(TEXT("closed now"), F.Door->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorPlacementTest, "VB.Unit.Interaction.PhysicalDoor.RotatedPlacement",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorPlacementTest::RunTest(const FString& Parameters)
{
	// 문 액터를 옮기고 90 도 돌리고, 경첩을 프레임에서 100 띄우고, 음수 각(-90)으로 연다. 경첩의 세계 자리 = (1000, 600).
	//   경첩 기준으로 돌면 45 도 자리의 상자에 알파 0.4403 에서 선다(따로 계산). 프레임 원점 기준으로 잘못 돌리면 상자에 닿지 않고 끝까지 간다.
	VBPhysicalDoorTest::FDoorWorld F;
	const FTransform Placement(FRotator(0.0f, 90.0f, 0.0f), FVector(1000.0, 500.0, 0.0));
	if (!F.Init(*this, EVBDoorMotion::Swing, true, Placement, FVector(100.0, 0.0, 0.0), -90.0f))
	{
		return false;
	}

	if (!TestFalse(TEXT("valid door"), F.Door->HasFault()))
	{
		AddInfo(F.Door->GetFaultReason());
		return false;
	}

	F.SpawnBox(FVector(1070.7, 670.7, 100.0), FVector(5.0), /*bBlocking=*/true);
	const uint32 Open = F.Door->RequestMove(true);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	const float Stop = FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door);
	TestEqual(TEXT("blocked in the rotated, offset placement"), F.Count(Open, EVBDoorMoveResult::Blocked), 1);
	TestTrue(TEXT("at the computed stop alpha"), Stop > 0.42f && Stop < 0.46f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorFilterTest, "VB.Unit.Interaction.PhysicalDoor.OnlyMutualBlockersStop",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorFilterTest::RunTest(const FString& Parameters)
{
	{
		// 겹침만 하는 영역 트리거 모양은 문을 세우지 않는다.
		VBPhysicalDoorTest::FDoorWorld F;
		if (!F.Init(*this))
		{
			return false;
		}

		F.SpawnBox(FVector(70.7, 70.7, 100.0), FVector(20.0), /*bBlocking=*/false);
		const uint32 Open = F.Door->RequestMove(true);
		FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
		TestEqual(TEXT("an overlap-only sensor does not stop the door"), F.Count(Open, EVBDoorMoveResult::Reached), 1);
	}

	{
		// 같은 자리, 크기의 막는 상자는 세운다.
		VBPhysicalDoorTest::FDoorWorld F;
		if (!F.Init(*this))
		{
			return false;
		}

		F.SpawnBox(FVector(70.7, 70.7, 100.0), FVector(20.0), /*bBlocking=*/true);
		const uint32 Open = F.Door->RequestMove(true);
		FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
		TestEqual(TEXT("a blocking solid stops it"), F.Count(Open, EVBDoorMoveResult::Blocked), 1);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorResultsTest, "VB.Unit.Interaction.PhysicalDoor.Results",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorResultsTest::RunTest(const FString& Parameters)
{
	{
		VBPhysicalDoorTest::FDoorWorld F;
		if (!F.Init(*this))
		{
			return false;
		}
		// 새 요청은 앞 요청을 한 번 Cancelled 로 끝낸다.
		const uint32 First = F.Door->RequestMove(true);
		FVBPhysicalDoorTestAccess::Advance(*F.Door, 0.3);
		const uint32 Second = F.Door->RequestMove(false);
		TestEqual(TEXT("superseded request cancelled once"), F.Count(First, EVBDoorMoveResult::Cancelled), 1);
		// 즉시 자세는 진행 중인 요청을 끝내고 자기 알림은 없다.
		const int32 Before = F.Results.Num();
		F.Door->SetPoseInstant(true);
		TestEqual(TEXT("instant pose cancels the pending request once"), F.Count(Second, EVBDoorMoveResult::Cancelled), 1);
		TestEqual(TEXT("and adds nothing else"), F.Results.Num(), Before + 1);
		F.Door->SetPoseInstant(true);
		TestEqual(TEXT("an idle instant pose emits nothing"), F.Results.Num(), Before + 1);
		TestTrue(TEXT("instant open is the saved state"), F.Door->IsOpen());
		// 이미 그 끝이면 받아들이고, 결과는 다음 틱에.
		const uint32 Same = F.Door->RequestMove(true);
		TestTrue(TEXT("zero-distance request accepted"), Same != 0);
		TestEqual(TEXT("no result before the next tick"), F.Count(Same, EVBDoorMoveResult::Reached), 0);
		F.Isolated.World->GetTimerManager().Tick(0.016f);
		TestEqual(TEXT("reached on the next tick"), F.Count(Same, EVBDoorMoveResult::Reached), 1);
	}

	{
		// 앞 요청의 Cancelled 를 받은 쪽이 그 안에서 또 요청해도 번호마다 결과는 정확히 하나다.
		VBPhysicalDoorTest::FDoorWorld F;
		if (!F.Init(*this))
		{
			return false;
		}

		uint32 First = 0;
		uint32 Inner = 0;
		F.Door->OnDoorMoveFinished.AddLambda([&F, &First, &Inner](uint32 Serial, EVBDoorMoveResult Result)
		{
			if (Serial == First && Result == EVBDoorMoveResult::Cancelled && Inner == 0)
			{
				Inner = F.Door->RequestMove(true);
			}
		});
		First = F.Door->RequestMove(true);
		FVBPhysicalDoorTestAccess::Advance(*F.Door, 0.3);
		const uint32 Outer = F.Door->RequestMove(false);
		TestTrue(TEXT("the listener made a request inside the Cancelled"), Inner != 0 && Inner != Outer);
		TestEqual(TEXT("first: one Cancelled"), F.Count(First, EVBDoorMoveResult::Cancelled), 1);
		TestEqual(TEXT("outer: one Cancelled (the newer inner request won)"), F.Count(Outer, EVBDoorMoveResult::Cancelled), 1);
		FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
		TestEqual(TEXT("inner: reached"), F.Count(Inner, EVBDoorMoveResult::Reached), 1);
		TestEqual(TEXT("exactly three results in all"), F.Results.Num(), 3);
	}

	{
		// 문짝 메시가 없으면 결함 - 모든 요청 거절, 상태 그대로.
		AddExpectedMessagePlain(TEXT("물리 문을 쓸 수 없다"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
		AddExpectedMessagePlain(TEXT("문 요청 거절"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
		VBPhysicalDoorTest::FDoorWorld F;
		if (!F.Init(*this, EVBDoorMotion::Swing, /*bWithMesh=*/false)) return false;
		TestTrue(TEXT("no leaf mesh is a fault"), F.Door->HasFault());
		FString Why;
		TestEqual(TEXT("refused"), F.Door->RequestMove(true, &Why), 0u);
		TestFalse(TEXT("refusal says why"), Why.IsEmpty());
		TestEqual(TEXT("refusal emits nothing"), F.Results.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorSlideTest, "VB.Unit.Interaction.PhysicalDoor.SlideStopsOnContact",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorSlideTest::RunTest(const FString& Parameters)
{
	VBPhysicalDoorTest::FDoorWorld F;
	if (!F.Init(*this, EVBDoorMotion::Slide))
	{
		return false;
	}
	// 기본 SlideOffset 은 +Y 로 120. 문짝(Y -2..2)이 Y 60 의 상자에 닿기 전에 멈춰야 한다.
	F.SpawnBox(FVector(60.0, 62.0, 100.0), FVector(10.0, 2.0, 10.0), /*bBlocking=*/true);
	const uint32 Open = F.Door->RequestMove(true);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestEqual(TEXT("slide blocked once"), F.Count(Open, EVBDoorMoveResult::Blocked), 1);
	TestTrue(TEXT("stopped before reaching the end"), FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door) < 1.0f);
	return true;
}

namespace VBPhysicalDoorStoryTest
{
	// 문은 원점, 점유 영역은 문의 쓸기에서 먼 곳. 사실 하나가 켜져 있는 동안 닫히는 문(ClosedWhile = [사실]).
	const FVector AreaCenter(1000.0, 0.0, 0.0);
	const FVector OutsideArea(2000.0, 0.0, 0.0);

	struct FStoryDoorWorld : VBPhysicalDoorTest::FDoorWorld
	{
		UVBStoryStateComponent* Story = nullptr;
		AVBAreaTrigger* Area = nullptr;
		ACharacter* Pawn = nullptr;
		FGameplayTag Fact;

		// 플레이를 시작한 폰은 월드를 지우기 전에 없앤다(EndPlay 없이 GC 되지 않게).
		~FStoryDoorWorld() { if (IsValid(Pawn)) Pawn->Destroy(); }

		// bWithStory 거짓 = 사실 저장소가 없는 월드. bWithArea = 점유 영역 + 그 밖에 선 플레이어. bConditions 거짓 = 사실 조건 없음.
		bool InitStory(FAutomationTestBase& Test, bool bWithStory, bool bWithArea, bool bFactAtStart, bool bConditions = true)
		{
			if (!SpawnDoor(Test))
			{
				return false;
			}

			UWorld* World = Isolated.World;
			Fact = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.ChapterOne"));
			if (bWithStory)
			{
				AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
				World->SetGameState(GameState);
				Story = NewObject<UVBStoryStateComponent>(GameState);
				GameState->AddInstanceComponent(Story);
				Story->RegisterComponent();
				// "저장된" 사실: 문이 시작하기 전에 넣어 둔다.
				if (bFactAtStart)
				{
					Story->SetFact(Fact, true);
				}
			}

			if (bWithArea)
			{
				Area = World->SpawnActor<AVBAreaTrigger>(AreaCenter, FRotator::ZeroRotator);
				if (!Test.TestNotNull(TEXT("occupancy area"), Area))
				{
					return false;
				}

				Area->DispatchBeginPlay();
				if (!SpawnPlayer(Test))
				{
					return false;
				}
			}

			TArray<FInstancedStruct> ClosedWhile;
			if (bConditions)
			{
				FVBCondition_Fact Condition;
				Condition.Fact = Fact;
				ClosedWhile.Add(FInstancedStruct::Make(Condition));
			}

			FVBPhysicalDoorTestAccess::ConfigureStory(*Door, ClosedWhile, Area);
			Door->GetInteractable()->SetInteractionEnabled(false);
			Door->DispatchBeginPlay();
			return true;
		}

		// 영역 겹침만 내는 플레이어 폰(영역, 조우 시험과 같은 캡슐 설정). 문짝과는 서로 막지 않는다.
		bool SpawnPlayer(FAutomationTestBase& Test)
		{
			FActorSpawnParameters Params;
			Params.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			Params.bDeferConstruction = true;
			Pawn = Isolated.World->SpawnActor<ACharacter>(OutsideArea, FRotator::ZeroRotator, Params);
			if (!Test.TestNotNull(TEXT("player pawn"), Pawn))
			{
				return false;
			}

			Pawn->AutoPossessAI = EAutoPossessAI::Disabled;
			Pawn->AutoPossessPlayer = EAutoReceiveInput::Disabled;
			Pawn->FinishSpawning(FTransform(OutsideArea));
			UCapsuleComponent* Capsule = Pawn->GetCapsuleComponent();
			Capsule->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
			Capsule->SetCollisionObjectType(ECC_Pawn);
			Capsule->SetCollisionResponseToAllChannels(ECR_Ignore);
			Capsule->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Overlap);
			Capsule->SetGenerateOverlapEvents(true);
			Pawn->GetMesh()->SetCollisionEnabled(ECollisionEnabled::NoCollision);
			Pawn->GetMesh()->SetGenerateOverlapEvents(false);
			APlayerController* Controller = Isolated.World->SpawnActor<APlayerController>();
			if (!Test.TestNotNull(TEXT("player controller"), Controller))
			{
				return false;
			}

			Controller->PlayerState = Isolated.World->SpawnActor<APlayerState>();
			Controller->Possess(Pawn);
			// 문은 월드의 플레이어 컨트롤러 목록으로 플레이어를 찾는다 - 고립 월드에서 스폰한 컨트롤러는 그 목록에 없을 수 있다.
			bool bListed = false;
			for (FConstPlayerControllerIterator It = Isolated.World->GetPlayerControllerIterator(); It; ++It)
			{
				bListed |= It->Get() == Controller;
			}

			if (!bListed)
			{
				Isolated.World->AddController(Controller);
			}
			// 엔진은 플레이를 시작하지 않은 액터의 겹침을 갱신하지 않는다(UPrimitiveComponent::UpdateOverlaps) - 영역, 문과 같이 시작시킨다.
			Pawn->DispatchBeginPlay();
			Capsule->UpdateOverlaps();
			return Test.TestTrue(TEXT("the player starts outside the area"), Controller->GetPawn() == Pawn && !Area->IsPawnInside(Pawn));
		}

		void MovePlayer(const FVector& Where) const
		{
			Pawn->SetActorLocation(Where);
			Pawn->GetCapsuleComponent()->UpdateOverlaps();
		}

		bool LastIs(EVBDoorMoveResult Result) const { return Results.Num() > 0 && Results.Last().Value == Result; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryEmptyTest, "VB.Unit.Interaction.PhysicalDoor.Story.EmptyFieldsUnchanged",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryEmptyTest::RunTest(const FString& Parameters)
{
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	if (!F.InitStory(*this, /*bWithStory=*/true, /*bWithArea=*/false, /*bFactAtStart=*/false, /*bConditions=*/false)) return false;
	TestFalse(TEXT("no story control"), F.Door->HasStoryControl());
	TestFalse(TEXT("valid door"), F.Door->HasFault());
	F.Story->SetFact(F.Fact, true);
	F.Story->SetFact(F.Fact, false);
	TestFalse(TEXT("story never started"), FVBPhysicalDoorTestAccess::StoryStarted(*F.Door));
	TestEqual(TEXT("fact changes request nothing"), F.Results.Num(), 0);
	TestFalse(TEXT("not moving"), F.Door->IsMoving());
	TestEqual(TEXT("still at the saved (closed) end"), FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryStartTest, "VB.Unit.Interaction.PhysicalDoor.Story.StartPoseFromFacts",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryStartTest::RunTest(const FString& Parameters)
{
	{
		// 저장된 끝은 닫힘(기본), 사실은 꺼짐 -> 바라는 열림에 바로 선다. 움직임, 알림 없음.
		VBPhysicalDoorStoryTest::FStoryDoorWorld F;
		if (!F.InitStory(*this, true, false, /*bFactAtStart=*/false)) return false;
		TestTrue(TEXT("started at BeginPlay"), FVBPhysicalDoorTestAccess::StoryStarted(*F.Door));
		TestTrue(TEXT("open at once"), F.Door->IsOpen() && FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door) == 1.0f);
		TestFalse(TEXT("no move"), F.Door->IsMoving());
		TestEqual(TEXT("no result"), F.Results.Num(), 0);
	}

	{
		// 사실이 이미 켜져 있다 -> 닫힘 그대로, 움직임 없음.
		VBPhysicalDoorStoryTest::FStoryDoorWorld F;
		if (!F.InitStory(*this, true, false, /*bFactAtStart=*/true)) return false;
		TestTrue(TEXT("closed"), !F.Door->IsOpen() && FVBPhysicalDoorTestAccess::LeafAlpha(*F.Door) == 0.0f);
		TestFalse(TEXT("no move"), F.Door->IsMoving());
		TestEqual(TEXT("no result"), F.Results.Num(), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryFollowTest, "VB.Unit.Interaction.PhysicalDoor.Story.FollowsConditions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryFollowTest::RunTest(const FString& Parameters)
{
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	if (!F.InitStory(*this, true, false, false))
	{
		return false;
	}

	if (!TestTrue(TEXT("starts open"), F.Door->IsOpen()))
	{
		return false;
	}

	F.Story->SetFact(F.Fact, true);
	TestTrue(TEXT("conditions true -> a close is under way"), F.Door->IsMoving());
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("closed"), F.LastIs(EVBDoorMoveResult::Reached) && !F.Door->IsOpen());
	F.Story->SetFact(F.Fact, false);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("conditions false -> open again"), F.LastIs(EVBDoorMoveResult::Reached) && F.Door->IsOpen());
	TestEqual(TEXT("exactly two moves"), F.Results.Num(), 2);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryBlockedTest, "VB.Unit.Interaction.PhysicalDoor.Story.BlockedRetries",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryBlockedTest::RunTest(const FString& Parameters)
{
	// 막힘 두 번(재시도 포함)에도 경고는 한 번.
	AddExpectedMessagePlain(TEXT("이야기가 바라는 자세로 가다 막혔다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	if (!F.InitStory(*this, true, false, false))
	{
		return false;
	}
	// 닫히는 길 한가운데(45 도, 반지름 100)에 막는 상자 - 기본 문 시험과 같은 자리.
	AActor* Blocker = F.SpawnBox(FVector(70.7, 70.7, 100.0), FVector(5.0), /*bBlocking=*/true);
	F.Story->SetFact(F.Fact, true);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("close blocked"), F.LastIs(EVBDoorMoveResult::Blocked) && F.Door->IsBlocked());
	TestTrue(TEXT("a retry is pending"), FVBPhysicalDoorTestAccess::RetryPending(*F.Door));
	FVBPhysicalDoorTestAccess::FireRetry(*F.Door);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("still blocked on the retry"), F.LastIs(EVBDoorMoveResult::Blocked));
	TestTrue(TEXT("retried again while still wanted"), FVBPhysicalDoorTestAccess::RetryPending(*F.Door));
	Blocker->Destroy();
	FVBPhysicalDoorTestAccess::FireRetry(*F.Door);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("closes once the blocker leaves"), F.LastIs(EVBDoorMoveResult::Reached) && !F.Door->IsOpen());
	TestFalse(TEXT("no retry after reaching"), FVBPhysicalDoorTestAccess::RetryPending(*F.Door));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryUnblockTest, "VB.Unit.Interaction.PhysicalDoor.Story.OpensWhenNoLongerWanted",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryUnblockTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("이야기가 바라는 자세로 가다 막혔다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	if (!F.InitStory(*this, true, false, false))
	{
		return false;
	}

	F.SpawnBox(FVector(70.7, 70.7, 100.0), FVector(5.0), /*bBlocking=*/true);
	F.Story->SetFact(F.Fact, true);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("close blocked"), F.LastIs(EVBDoorMoveResult::Blocked));
	// 막힌 채 조건이 거짓이 된다 - 닫기를 그만두고 바로 연다(상자는 그대로, 여는 길은 비었다).
	F.Story->SetFact(F.Fact, false);
	TestFalse(TEXT("the close retry is dropped"), FVBPhysicalDoorTestAccess::RetryPending(*F.Door));
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("opened"), F.LastIs(EVBDoorMoveResult::Reached) && F.Door->IsOpen());
	TestFalse(TEXT("no retry left"), FVBPhysicalDoorTestAccess::RetryPending(*F.Door));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryOccupancyTest, "VB.Unit.Interaction.PhysicalDoor.Story.ClosedOnlyWhileOccupied",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryOccupancyTest::RunTest(const FString& Parameters)
{
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	// 조건(사실)은 처음부터 참, 플레이어는 영역 밖 - 밖에서 불러온 경우.
	if (!F.InitStory(*this, true, /*bWithArea=*/true, /*bFactAtStart=*/true)) return false;
	TestTrue(TEXT("nobody inside -> open even though the conditions hold"), F.Door->IsOpen());
	TestEqual(TEXT("start pose without a move"), F.Results.Num(), 0);
	// 엔진은 월드가 플레이를 시작한 뒤에만 겹침 알림을 낸다(UPrimitiveComponent::BeginComponentOverlap) - 영역 점유 시험과 같이 월드를 시작한다.
	AGameStateBase* GameState = F.Isolated.World->GetGameState();
	if (!TestNotNull(TEXT("game state"), GameState))
	{
		return false;
	}

	GameState->HandleBeginPlay();
	if (!TestTrue(TEXT("world has begun play"), F.Isolated.World->HasBegunPlay()))
	{
		return false;
	}

	int32 OccupancySignals = 0;
	F.Area->OnOccupancyChanged.AddLambda([&OccupancySignals]() { ++OccupancySignals; });
	F.MovePlayer(VBPhysicalDoorStoryTest::AreaCenter);
	TestTrue(TEXT("player inside"), F.Area->IsPawnInside(F.Pawn));
	// 진단: 영역이 알렸나, 문이 플레이어를 찾나(컨트롤러 목록) - 실패하면 어느 고리가 끊겼는지 보인다.
	TestTrue(FString::Printf(TEXT("area signalled occupancy (%d)"), OccupancySignals), OccupancySignals > 0);
	bool bControllerListed = false;
	for (FConstPlayerControllerIterator It = F.Door->GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		bControllerListed |= It->Get() && It->Get()->GetPawn() == F.Pawn;
	}

	TestTrue(TEXT("door can find the player through the controller list"), bControllerListed);
	TestTrue(TEXT("entering starts a close"), F.Door->IsMoving());
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("closed while occupied"), F.LastIs(EVBDoorMoveResult::Reached) && !F.Door->IsOpen());
	F.MovePlayer(VBPhysicalDoorStoryTest::OutsideArea);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("leaving opens it"), F.LastIs(EVBDoorMoveResult::Reached) && F.Door->IsOpen());
	// 안에 있어도 조건이 거짓이면 열린 채다.
	F.Story->SetFact(F.Fact, false);
	F.MovePlayer(VBPhysicalDoorStoryTest::AreaCenter);
	TestFalse(TEXT("occupied but conditions false -> no move"), F.Door->IsMoving());
	TestTrue(TEXT("still open"), F.Door->IsOpen());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPhysicalDoorStoryNoOwnerTest, "VB.Unit.Interaction.PhysicalDoor.Story.NoStoryStaysOpen",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPhysicalDoorStoryNoOwnerTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("이야기 조건을 따를 수 없어 문을 연다"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	VBPhysicalDoorStoryTest::FStoryDoorWorld F;
	if (!F.InitStory(*this, /*bWithStory=*/false, false, false)) return false;
	TestFalse(TEXT("waits for the story while it is missing"), FVBPhysicalDoorTestAccess::StoryStarted(*F.Door));
	TestFalse(TEXT("no pose change while waiting"), F.Door->IsOpen() || F.Door->IsMoving());
	FVBPhysicalDoorTestAccess::ExpireStoryStartup(*F.Door);
	FVBPhysicalDoorTestAccess::Advance(*F.Door, 10.0);
	TestTrue(TEXT("gives up and opens"), F.LastIs(EVBDoorMoveResult::Reached) && F.Door->IsOpen());
	// 한 번 포기한 뒤의 시작 틱은 아무것도 하지 않는다 - 오류는 한 줄.
	FVBPhysicalDoorTestAccess::ExpireStoryStartup(*F.Door);
	TestFalse(TEXT("never starts afterwards"), FVBPhysicalDoorTestAccess::StoryStarted(*F.Door));
	return true;
}
#endif
