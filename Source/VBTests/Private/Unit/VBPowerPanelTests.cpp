// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Components/BoxComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Engine/PointLight.h"
#include "Engine/StaticMesh.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"
#include "Interaction/VBInteractableComponent.h"
#include "Interaction/VBPhysicalDoor.h"
#include "Interaction/VBPowerPanel.h"
#include "Interaction/VBPowerSwitch.h"
#include "Components/LightComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "TimerManager.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Unit/VBStoryTestWorld.h"
#include "Unit/VBPhysicalDoorTestAccess.h"

// 전력 제어함 시험. 공급 1, 회로 L(작업등), D(출구 물리 문), 배분표 [ {L}, {}, {D} ].
//   시간은 문의 움직임 시작 시각을 과거로 돌려 흘린다. 클라 복제와 거절 글 표시는 덮지 않는다.
class FVBPowerPanelTestAccess
{
public:
	static void Configure(AVBPowerPanel& Panel, const TArray<FVBPowerCircuit>& Circuits, const TArray<AVBPowerSwitch*>& Switches,
		FGameplayTag Counter, FGameplayTag Opened, const TArray<FInstancedStruct>& OnOpened)
	{
		Panel.Circuits = Circuits;
		Panel.Allocations = {FVBPowerAllocation{{TEXT("L")}}, FVBPowerAllocation{{}}, FVBPowerAllocation{{TEXT("D")}}};
		Panel.Supply = 1;
		Panel.Switches.Reset();
		for (AVBPowerSwitch* Switch : Switches)
		{
			Panel.Switches.Add(Switch);
		}

		Panel.AllocationCounter = Counter;
		Panel.OpenedFact = Opened;
		Panel.OnOpenedEffects = OnOpened;
	}

	static bool OpenFailed(const AVBPowerPanel& Panel) { return Panel.bOpenFailed; }
	static bool CloseFailed(const AVBPowerPanel& Panel) { return Panel.bCloseFailed; }
	static uint32 DoorSerial(const AVBPowerPanel& Panel) { return Panel.DoorSerial; }
	static bool WaitingForStart(const AVBPowerPanel& Panel) { return !Panel.bStarted && Panel.GetWorldTimerManager().TimerExists(Panel.StartupTimer); }
	static void StartupTick(AVBPowerPanel& Panel) { Panel.HandleStartupTick(); }
};

namespace VBPowerPanelTest
{
	struct FPanelWorld
	{
		FVBIsolatedGameWorld Isolated;
		UVBStoryStateComponent* Story = nullptr;
		AVBPhysicalDoor* Door = nullptr;
		AVBPowerPanel* Panel = nullptr;
		AVBPowerSwitch* SwitchL = nullptr;
		AVBPowerSwitch* SwitchD = nullptr;
		APointLight* WorkLight = nullptr;
		FGameplayTag Counter;
		FGameplayTag Opened;
		FGameplayTag Reward;

		bool Init(FAutomationTestBase& Test, bool bPanelFirst = false, int32 SavedCounter = 0, bool bSavedOpened = false)
		{
			if (!Isolated.Init(Test, /*bPhysicsScene=*/true)) return false;
			UWorld* World = Isolated.World;
			AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
			World->SetGameState(GameState);
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();
			Counter = FGameplayTag::RequestGameplayTag(TEXT("Story.Prologue.NursePowerAllocation"));
			Opened = FGameplayTag::RequestGameplayTag(TEXT("Story.Prologue.ExitDoorOpened"));
			Reward = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Talked"));
			// 저장된 상태: 시작 전에 사실 저장소에 넣어 둔다.
			Story->SetCounter(Counter, SavedCounter);
			if (bSavedOpened)
			{
				Story->SetFact(Opened, true);
			}

			// 문: 경첩 원점, 문짝 120 x 4 x 200, 90 도. 제어함이 모는 문 = 상호작용, 저장 끔.
			FActorSpawnParameters Deferred;
			Deferred.bDeferConstruction = true;
			Door = World->SpawnActor<AVBPhysicalDoor>(FVector::ZeroVector, FRotator::ZeroRotator, Deferred);
			UStaticMeshComponent* Leaf = Door->FindComponentByClass<UStaticMeshComponent>();
			Leaf->SetStaticMesh(LoadObject<UStaticMesh>(nullptr, TEXT("/Engine/BasicShapes/Cube.Cube")));
			Leaf->SetRelativeLocation(FVector(60.0, 0.0, 100.0));
			Leaf->SetRelativeScale3D(FVector(1.2, 0.04, 2.0));
			Door->GetInteractable()->SetInteractionEnabled(false);
			Door->GetInteractable()->SetSaveState(false);
			Door->FinishSpawning(FTransform::Identity);

			WorkLight = World->SpawnActor<APointLight>(FVector(500.0, 0.0, 300.0), FRotator::ZeroRotator);
			Panel = World->SpawnActor<AVBPowerPanel>(FVector(300.0, 300.0, 0.0), FRotator::ZeroRotator, Deferred);
			SwitchL = SpawnSwitch(TEXT("L"));
			SwitchD = SpawnSwitch(TEXT("D"));
			if (!Test.TestTrue(TEXT("actors"), Door && WorkLight && Panel && SwitchL && SwitchD))
			{
				return false;
			}

			FVBPowerCircuit L{TEXT("L"), {WorkLight}, nullptr};
			FVBPowerCircuit D{TEXT("D"), {}, Door};
			FInstancedStruct Add;
			{
				FVBEffect_AddCounter Effect;
				Effect.Counter = Reward;
				Add = FInstancedStruct::Make(Effect);
			}

			FVBPowerPanelTestAccess::Configure(*Panel, {L, D}, {SwitchL, SwitchD}, Counter, Opened, {Add});
			Panel->FinishSpawning(FTransform(FVector(300.0, 300.0, 0.0)));

			SwitchL->DispatchBeginPlay();
			SwitchD->DispatchBeginPlay();
			if (bPanelFirst)
			{
				Panel->DispatchBeginPlay();
				if (!Test.TestTrue(TEXT("the panel waits for the door"), FVBPowerPanelTestAccess::WaitingForStart(*Panel)))
				{
					return false;
				}

				Door->DispatchBeginPlay();
				// 기다림 틱을 직접 부른다. 이 프레임에 건 타이머는 FTimerManager::Tick 한 번으로는 돌지 않고(끝에서야 활성화),
				//   같은 프레임의 두 번째 Tick 은 바로 돌아온다(TimerManager.cpp 712-725, 1136, 1377-1388).
				FVBPowerPanelTestAccess::StartupTick(*Panel);
			}
			else
			{
				Door->DispatchBeginPlay();
				Panel->DispatchBeginPlay();
			}

			return true;
		}

		AVBPowerSwitch* SpawnSwitch(const TCHAR* Circuit) const
		{
			FActorSpawnParameters Deferred;
			Deferred.bDeferConstruction = true;
			AVBPowerSwitch* Switch = Isolated.World->SpawnActor<AVBPowerSwitch>(FVector::ZeroVector, FRotator::ZeroRotator, Deferred);
			if (!Switch)
			{
				return nullptr;
			}

			if (FNameProperty* Id = FindFProperty<FNameProperty>(AVBPowerSwitch::StaticClass(), TEXT("CircuitId")))
			{
				Id->SetPropertyValue_InContainer(Switch, FName(Circuit));
			}

			if (FObjectPropertyBase* Owner = FindFProperty<FObjectPropertyBase>(AVBPowerSwitch::StaticClass(), TEXT("Panel")))
			{
				Owner->SetObjectPropertyValue_InContainer(Switch, Panel);
			}

			Switch->FinishSpawning(FTransform::Identity);
			return Switch;
		}

		int32 Allocation() const { return Story->GetCounter(Counter); }
		bool LightOn() const { return WorkLight->GetLightComponent()->IsVisible(); }
		void Press(AVBPowerSwitch* Switch) const { Panel->RequestToggle(*Switch, nullptr); }
		void FinishDoor() const { FVBPhysicalDoorTestAccess::Advance(*Door, 10.0); }
		AActor* SpawnBlocker(const FVector& Center) const
		{
			AActor* Actor = Isolated.World->SpawnActor<AActor>();
			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->AddInstanceComponent(Box);
			Actor->SetRootComponent(Box);
			Box->SetBoxExtent(FVector(5.0));
			Box->SetCollisionObjectType(ECC_WorldDynamic);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Actor->SetActorLocation(Center);
			Box->RegisterComponent();
			return Actor;
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPowerPanelAllocationTest, "VB.Unit.Interaction.PowerPanel.Allocation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPowerPanelAllocationTest::RunTest(const FString& Parameters)
{
	VBPowerPanelTest::FPanelWorld F;
	if (!F.Init(*this))
	{
		return false;
	}

	if (!TestFalse(TEXT("valid placement"), F.Panel->HasFault()))
	{
		AddInfo(F.Panel->GetFaultReason());
		return false;
	}

	TestEqual(TEXT("P0 at start"), F.Allocation(), 0);
	TestTrue(TEXT("P0: work light on, L switch on, D switch off"), F.LightOn() && F.SwitchL->GetInteractable()->IsActivated()
		&& !F.SwitchD->GetInteractable()->IsActivated());
	TestFalse(TEXT("door closed"), F.Door->IsOpen());

	F.Press(F.SwitchD);
	TestEqual(TEXT("P0 + D is over supply: refused, still P0"), F.Allocation(), 0);
	F.Press(F.SwitchL);
	TestEqual(TEXT("L off -> P1"), F.Allocation(), 1);
	TestFalse(TEXT("P1: work light off"), F.LightOn());
	F.Press(F.SwitchL);
	TestEqual(TEXT("L on again -> P0 (no extra energy)"), F.Allocation(), 0);
	F.Press(F.SwitchL);
	F.Press(F.SwitchD);
	TestEqual(TEXT("P1 + D -> P2"), F.Allocation(), 2);
	TestTrue(TEXT("P2 starts the door opening"), FVBPowerPanelTestAccess::DoorSerial(*F.Panel) != 0);
	F.Press(F.SwitchL);
	TestEqual(TEXT("no distribution while the door moves"), F.Allocation(), 2);
	F.FinishDoor();
	TestTrue(TEXT("door reached open -> P3 fact"), F.Door->IsOpen() && F.Story->HasFact(F.Opened));
	TestEqual(TEXT("opened effects once"), F.Story->GetCounter(F.Reward), 1);
	F.Press(F.SwitchD);
	TestEqual(TEXT("after P3 the panel only answers (latch)"), F.Allocation(), 2);
	TestTrue(TEXT("door stays open"), F.Door->IsOpen());
	TestEqual(TEXT("no second reward"), F.Story->GetCounter(F.Reward), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPowerPanelBlockedTest, "VB.Unit.Interaction.PowerPanel.BlockedOpenAndCloseRetry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPowerPanelBlockedTest::RunTest(const FString& Parameters)
{
	VBPowerPanelTest::FPanelWorld F;
	if (!F.Init(*this))
	{
		return false;
	}
	// 여는 길(45 도, 반지름 100)을 막는다.
	AActor* OpenBlocker = F.SpawnBlocker(FVector(70.7, 70.7, 100.0));
	F.Press(F.SwitchL);
	F.Press(F.SwitchD);
	F.FinishDoor();
	TestTrue(TEXT("open blocked -> failure held"), FVBPowerPanelTestAccess::OpenFailed(*F.Panel) && F.Door->IsBlocked());
	TestEqual(TEXT("allocation stays P2"), F.Allocation(), 2);
	TestFalse(TEXT("no P3"), F.Story->HasFact(F.Opened));
	// 상관없는 사실이 바뀌어도 붙든 실패를 스스로 다시 시도하지 않는다.
	F.Story->SetFact(F.Reward, true);
	TestEqual(TEXT("a story notification does not start a new request"), FVBPowerPanelTestAccess::DoorSerial(*F.Panel), 0u);

	// D 를 끄면(P1) 실패가 풀리고 닫는다 - 닫는 길(문짝 뒤 20 도)도 막아 둔다.
	AActor* CloseBlocker = F.SpawnBlocker(FVector(98.5, 17.4, 100.0));
	F.Press(F.SwitchD);
	TestEqual(TEXT("D off -> P1"), F.Allocation(), 1);
	F.FinishDoor();
	TestTrue(TEXT("close blocked -> close failure held, open failure cleared"), FVBPowerPanelTestAccess::CloseFailed(*F.Panel)
		&& !FVBPowerPanelTestAccess::OpenFailed(*F.Panel));
	TestEqual(TEXT("still P1"), F.Allocation(), 1);

	// 둘 다 비우고 D 를 다시 켜면 지금 자리에서 새로 열고, 끝까지 열리면 P3.
	OpenBlocker->Destroy();
	CloseBlocker->Destroy();
	F.Press(F.SwitchD);
	TestEqual(TEXT("D on -> P2"), F.Allocation(), 2);
	TestTrue(TEXT("a fresh open request"), FVBPowerPanelTestAccess::DoorSerial(*F.Panel) != 0);
	F.FinishDoor();
	TestTrue(TEXT("opened -> P3"), F.Door->IsOpen() && F.Story->HasFact(F.Opened));
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPowerPanelForeignRequestTest, "VB.Unit.Interaction.PowerPanel.ForeignRequest",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPowerPanelForeignRequestTest::RunTest(const FString& Parameters)
{
	VBPowerPanelTest::FPanelWorld F;
	if (!F.Init(*this))
	{
		return false;
	}

	F.Press(F.SwitchL);
	F.Press(F.SwitchD);
	// 다른 쪽이 여는 중인 문을 닫으라 한다: 제어함은 제 요청의 Cancelled 를 받고 다시 열기를 낸다.
	//   그 다시 열기가 다른 쪽 요청을 Cancelled 로 끝내는데, 그 결과는 제어함의 것이 아니다(작업 번호로 가린다).
	const uint32 Foreign = F.Door->RequestMove(false, nullptr);
	const uint32 Own = FVBPowerPanelTestAccess::DoorSerial(*F.Panel);
	TestTrue(TEXT("the panel asks again with its own serial"), Foreign != 0 && Own != 0 && Own != Foreign);
	TestEqual(TEXT("allocation stays P2"), F.Allocation(), 2);
	F.FinishDoor();
	TestTrue(TEXT("its own open reaches the end -> P3"), F.Door->IsOpen() && F.Story->HasFact(F.Opened));
	TestEqual(TEXT("opened effects once"), F.Story->GetCounter(F.Reward), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPowerPanelRestoreTest, "VB.Unit.Interaction.PowerPanel.Restore",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPowerPanelRestoreTest::RunTest(const FString& Parameters)
{
	{
		// P2 저장, 제어함이 문보다 먼저 시작: 닫힌 자리에서 열기를 다시 한다.
		VBPowerPanelTest::FPanelWorld F;
		if (!F.Init(*this, /*bPanelFirst=*/true, /*SavedCounter=*/2)) return false;
		TestTrue(TEXT("P2 load replays the opening"), FVBPowerPanelTestAccess::DoorSerial(*F.Panel) != 0 && !F.Door->IsOpen());
		F.FinishDoor();
		TestTrue(TEXT("then completes"), F.Story->HasFact(F.Opened));
	}

	{
		// P3 저장: 열린 끝으로 바로, 보상 없음.
		VBPowerPanelTest::FPanelWorld F;
		if (!F.Init(*this, false, 2, /*bSavedOpened=*/true)) return false;
		TestTrue(TEXT("P3 load stands open at once"), F.Door->IsOpen() && FVBPowerPanelTestAccess::DoorSerial(*F.Panel) == 0);
		TestEqual(TEXT("no completion reward on load"), F.Story->GetCounter(F.Reward), 0);
	}

	{
		// 배분표 밖 카운터는 처음으로 고친다.
		AddExpectedMessagePlain(TEXT("배분표 밖"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
		VBPowerPanelTest::FPanelWorld F;
		if (!F.Init(*this, false, 7))
		{
			return false;
		}

		TestEqual(TEXT("out-of-table counter repaired to P0"), F.Allocation(), 0);
	}

	{
		// 열림 사실 + 문 없는 배분: 사실이 이긴다.
		AddExpectedMessagePlain(TEXT("문 회로가 없다"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
		VBPowerPanelTest::FPanelWorld F;
		if (!F.Init(*this, false, 0, true))
		{
			return false;
		}

		TestEqual(TEXT("opened fact wins: allocation repaired to P2"), F.Allocation(), 2);
		TestTrue(TEXT("door open"), F.Door->IsOpen());
		TestEqual(TEXT("no reward"), F.Story->GetCounter(F.Reward), 0);
	}

	return true;
}
// 사실 저장소는 묶음이 따라간다: 사라지면 장치를 그대로 두고(배분 0 으로 읽지 않는다), 바뀌면 새 저장소의 배분을 맞춘다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPowerPanelStorySourceTest, "VB.Unit.Interaction.PowerPanel.StorySource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBPowerPanelStorySourceTest::RunTest(const FString& Parameters)
{
	VBPowerPanelTest::FPanelWorld F;
	if (!F.Init(*this))
	{
		return false;
	}

	if (!TestFalse(TEXT("valid placement"), F.Panel->HasFault()))
	{
		AddInfo(F.Panel->GetFaultReason());
		return false;
	}

	TestTrue(TEXT("P0 at start: work light on"), F.LightOn());

	UWorld* World = F.Isolated.World;
	World->SetGameState(nullptr);
	F.Press(F.SwitchL);
	TestEqual(TEXT("no source: the press writes nothing"), F.Allocation(), 0);
	TestTrue(TEXT("no source: the light is left as it was"), F.LightOn());

	// 저장된 배분 P1 을 단 채 도착하는 저장소 - 스폰 전에 값을 넣을 수 없어 도착 뒤 바꾸고 알림으로 따라가게 한다.
	UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*World);
	if (!TestNotNull(TEXT("replacement story"), Story))
	{
		return false;
	}

	World->SetGameState(CastChecked<AGameStateBase>(Story->GetOwner()));
	Story->SetCounter(F.Counter, 1);
	TestFalse(TEXT("replaced source: its allocation P1 turns the work light off"), F.LightOn());
	TestFalse(TEXT("replaced source: L switch follows P1"), F.SwitchL->GetInteractable()->IsActivated());
	return true;
}
#endif
