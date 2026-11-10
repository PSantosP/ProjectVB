#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "Engine/Engine.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "Interaction/VBInteractorComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Quest/VBQuestDefinition.h"
#include "Narrative/Quest/VBQuestLogComponent.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Player/VBPlayerController.h"
#include "Player/VBPlayerState.h"
#include "UI/Notice/VBNoticeProducerComponent.h"
#include "UI/Notice/VBNoticeSubsystem.h"

// 알림 생산자의 배선 - 실제 사건을 듣고, 소유 로컬 플레이어의 알림 줄에, 한 번 넣는가. 문구 함수만으로는 배선 실수를 못 잡는다.
// 못 보는 것: 저장됨(시험 월드에 저장 서브시스템이 없다), 성장 레벨업(VBGrowthTests 몫), 원격 PC, 퀘스트 RPC, 복제 순서.
class FVBNoticeProducerTestAccess
{
public:
	static void BeginPlay(UVBNoticeProducerComponent& Producer) { Producer.BeginPlay(); }
	static void EndPlay(UVBNoticeProducerComponent& Producer) { Producer.EndPlay(EEndPlayReason::RemovedFromWorld); }
	static const APlayerState* BoundPlayerState(const UVBNoticeProducerComponent& Producer) { return Producer.BoundPlayerState.Get(); }
	static void ClearTutorialFormat(UVBNoticeProducerComponent& Producer) { Producer.TutorialCompletedFormat = FText::GetEmpty(); }
	static void RestoreTutorialFormat(UVBNoticeProducerComponent& Producer)
	{
		Producer.TutorialCompletedFormat = GetDefault<UVBNoticeProducerComponent>()->TutorialCompletedFormat;
	}
};

namespace
{
	struct FNoticeProducerFixture
	{
		UWorld* World = nullptr;
		ULocalPlayer* LocalPlayer = nullptr;
		AVBPlayerController* Controller = nullptr;
		UVBNoticeProducerComponent* Producer = nullptr;
		UVBNoticeSubsystem* Notices = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			const UWorld::InitializationValues Values = UWorld::InitializationValues()
				.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
			World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Values);
			if (!Test.TestNotNull(TEXT("isolated world"), World))
			{
				return false;
			}

			World->GetOutermost()->SetFlags(RF_Transient);
			if (!Test.TestNotNull(TEXT("engine"), GEngine))
			{
				return false;
			}

			GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
			Controller = World->SpawnActor<AVBPlayerController>();
			if (!Test.TestNotNull(TEXT("controller"), Controller))
			{
				return false;
			}
			// 실제 로컬 플레이어 문맥. PlayerAdded 가 로컬 플레이어 서브시스템(UI 관리자, 알림 줄)을 초기화한다(엔진 LocalPlayer.cpp PlayerAdded).
			LocalPlayer = NewObject<ULocalPlayer>(GEngine, NAME_None, RF_Transient);
			Controller->Player = LocalPlayer;
			LocalPlayer->PlayerController = Controller;
			LocalPlayer->PlayerAdded(nullptr, 0);
			Notices = UVBNoticeSubsystem::Get(Controller);
			Producer = Controller->FindComponentByClass<UVBNoticeProducerComponent>();
			return Test.TestNotNull(TEXT("notice sink of the owning local player"), Notices)
				&& Test.TestNotNull(TEXT("producer component on the controller"), Producer);
		}

		AVBPlayerState* SpawnPlayerState(FAutomationTestBase& Test) const
		{
			AVBPlayerState* PlayerState = World->SpawnActor<AVBPlayerState>();
			Test.TestNotNull(TEXT("player state"), PlayerState);
			return PlayerState;
		}

		// PlayerState 가 정해졌다는 신호를 실제 길(OnRep -> BroadcastPlayerStateSet)로 보낸다.
		void SetPlayerState(APlayerState* PlayerState) const
		{
			Controller->PlayerState = PlayerState;
			Controller->OnRep_PlayerState();
		}

		int32 CountText(const FString& Needle) const
		{
			int32 Count = 0;
			for (const FVBNoticeEntry& Entry : Notices->GetEntries())
			{
				Count += Entry.Text.ToString().Contains(Needle) ? 1 : 0;
			}

			return Count;
		}

		const FVBNoticeEntry* FindByKey(FName MergeKey) const
		{
			return Notices->GetEntries().FindByPredicate([MergeKey](const FVBNoticeEntry& Entry) { return Entry.MergeKey == MergeKey; });
		}

		void Shutdown()
		{
			if (LocalPlayer)
			{
				LocalPlayer->PlayerRemoved();
				if (Controller)
				{
					Controller->Player = nullptr;
				}

				LocalPlayer->PlayerController = nullptr;
			}

			if (World)
			{
				if (GEngine)
				{
					GEngine->DestroyWorldContext(World);
				}

				World->DestroyWorld(false);
			}
		}
	};

	void CompleteTutorial(FAutomationTestBase& Test, UVBTutorialComponent& Tutorial, const FText& Title)
	{
		// 주인 클라 RPC 가 큐에 넣는 길 그대로(Standalone 이라 로컬에서 실행된다). 함수가 비공개라 이름으로 부른다.
		//   이름이 바뀌어 못 찾으면 조용히 넘어가지 않는다 - 뒤의 "안 꺼냄" 단언이 허공에서 통과한다.
		UFunction* Function = Tutorial.FindFunction(TEXT("ClientCompletionNotice"));
		if (!Test.TestNotNull(TEXT("tutorial completion RPC found by name"), Function))
		{
			return;
		}

		struct { FText Title; } Params{Title};
		Tutorial.ProcessEvent(Function, &Params);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBNoticeProducerWiringTest, "VB.Unit.Notice.ProducerWiring",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBNoticeProducerWiringTest::RunTest(const FString& Parameters)
{
	using Access = FVBNoticeProducerTestAccess;
	FNoticeProducerFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	UVBNoticeProducerComponent& Producer = *Fixture.Producer;

	AVBPlayerState* StateA = Fixture.SpawnPlayerState(*this);
	AVBPlayerState* StateB = Fixture.SpawnPlayerState(*this);
	UVBItemDefinition* Bandage = NewObject<UVBItemDefinition>(GetTransientPackage(), TEXT("VBUnitNoticeBandage"), RF_Transient);
	UVBQuestDefinition* Quest = NewObject<UVBQuestDefinition>(GetTransientPackage(), TEXT("VBUnitNoticeQuest"), RF_Transient);
	APawn* PawnA = Fixture.World->SpawnActor<APawn>();
	APawn* PawnB = Fixture.World->SpawnActor<APawn>();
	if (!StateA || !StateB || !PawnA || !PawnB)
	{
		Fixture.Shutdown();
		return false;
	}

	Bandage->DisplayName = FText::FromString(TEXT("UnitBandage"));
	Quest->Title = FText::FromString(TEXT("UnitQuest"));
	UVBInteractorComponent* InteractorA = NewObject<UVBInteractorComponent>(PawnA);
	InteractorA->RegisterComponent();
	UVBInteractorComponent* InteractorB = NewObject<UVBInteractorComponent>(PawnB);
	InteractorB->RegisterComponent();
	UVBInventoryComponent* InventoryA = UVBInventoryComponent::FindFor(StateA);
	UVBQuestLogComponent* QuestLogA = UVBQuestLogComponent::FindFor(StateA);
	UVBTutorialComponent* TutorialA = UVBTutorialComponent::FindFor(StateA);
	UVBInventoryComponent* InventoryB = UVBInventoryComponent::FindFor(StateB);
	if (!TestNotNull(TEXT("inventory A"), InventoryA) || !TestNotNull(TEXT("quest log A"), QuestLogA)
		|| !TestNotNull(TEXT("tutorial A"), TutorialA) || !TestNotNull(TEXT("inventory B"), InventoryB))
	{
		Fixture.Shutdown();
		return false;
	}

	// 튜토리얼 큐: 묶기 전에 끝난 안내 - 문구가 비면 꺼내지 않고 두었다가, 문구가 있는 첫 묶기에서 한 번.
	CompleteTutorial(*this, *TutorialA, FText::FromString(TEXT("UnitTutorialEarly")));
	Access::ClearTutorialFormat(Producer);
	Access::BeginPlay(Producer);
	Fixture.SetPlayerState(StateA);
	TestEqual(TEXT("empty format leaves the tutorial queue"), Fixture.CountText(TEXT("UnitTutorialEarly")), 0);
	Access::RestoreTutorialFormat(Producer);
	TutorialA->OnCompletionNoticesChanged.Broadcast();
	TestEqual(TEXT("queued tutorial completion delivered once"), Fixture.CountText(TEXT("UnitTutorialEarly")), 1);
	TutorialA->OnCompletionNoticesChanged.Broadcast();
	TestEqual(TEXT("queue was taken - not delivered twice"), Fixture.CountText(TEXT("UnitTutorialEarly")), 1);
	TestTrue(TEXT("bound to player state A"), Access::BoundPlayerState(Producer) == StateA);

	// 아이템: 같은 아이템 연달아 = 한 줄, 수 누적. 같은 PlayerState 신호를 한 번 더 보내도 구독이 늘지 않는다.
	Fixture.SetPlayerState(StateA);
	InventoryA->OnItemAcquired.Broadcast(Bandage, 2);
	InventoryA->OnItemAcquired.Broadcast(Bandage, 1);
	const FVBNoticeEntry* ItemLine = Fixture.FindByKey(Bandage->GetFName());
	if (TestNotNull(TEXT("item line in the owning local player's feed"), ItemLine))
	{
		TestEqual(TEXT("item merged count (repeat signal did not double-subscribe)"), ItemLine->Count, 3);
		TestTrue(TEXT("item category"), ItemLine->Category == EVBNoticeCategory::Item);
		TestTrue(TEXT("item text uses the merged count"), ItemLine->Text.ToString().Contains(TEXT("UnitBandage"))
			&& ItemLine->Text.ToString().Contains(TEXT("3")));
	}

	// 퀘스트: 사건당 한 줄.
	QuestLogA->OnQuestNotice.Broadcast(Quest, EVBQuestNotice::Started);
	TestEqual(TEXT("quest notice once"), Fixture.CountText(TEXT("UnitQuest")), 1);

	// 폰 교체: A -> null -> B. A 의 거절은 무시, B 의 거절은 한 줄.
	Fixture.Controller->Possess(PawnA);
	InteractorA->OnInteractionRefused.Broadcast(FText::FromString(TEXT("UnitRefusedA")));
	TestEqual(TEXT("refusal from possessed pawn A"), Fixture.CountText(TEXT("UnitRefusedA")), 1);
	Fixture.Controller->UnPossess();
	Fixture.Controller->Possess(PawnB);
	InteractorA->OnInteractionRefused.Broadcast(FText::FromString(TEXT("UnitRefusedStaleA")));
	InteractorB->OnInteractionRefused.Broadcast(FText::FromString(TEXT("UnitRefusedB")));
	TestEqual(TEXT("old pawn A no longer reports"), Fixture.CountText(TEXT("UnitRefusedStaleA")), 0);
	TestEqual(TEXT("new pawn B reports once"), Fixture.CountText(TEXT("UnitRefusedB")), 1);

	// PlayerState 교체: A -> B. A 의 사건은 더 오지 않고 B 는 온다.
	Fixture.SetPlayerState(StateB);
	TestTrue(TEXT("rebound to player state B"), Access::BoundPlayerState(Producer) == StateB);
	UVBItemDefinition* Stale = NewObject<UVBItemDefinition>(GetTransientPackage(), TEXT("VBUnitNoticeStale"), RF_Transient);
	Stale->DisplayName = FText::FromString(TEXT("UnitStaleItem"));
	InventoryA->OnItemAcquired.Broadcast(Stale, 1);
	TestNull(TEXT("old player state A no longer reports"), Fixture.FindByKey(Stale->GetFName()));
	UVBItemDefinition* Fresh = NewObject<UVBItemDefinition>(GetTransientPackage(), TEXT("VBUnitNoticeFresh"), RF_Transient);
	Fresh->DisplayName = FText::FromString(TEXT("UnitFreshItem"));
	InventoryB->OnItemAcquired.Broadcast(Fresh, 1);
	TestNotNull(TEXT("new player state B reports"), Fixture.FindByKey(Fresh->GetFName()));

	// EndPlay: 뒤의 사건은 전부 무시. 큐는 비우지 않는다.
	Access::EndPlay(Producer);
	UVBItemDefinition* After = NewObject<UVBItemDefinition>(GetTransientPackage(), TEXT("VBUnitNoticeAfter"), RF_Transient);
	After->DisplayName = FText::FromString(TEXT("UnitAfterItem"));
	InventoryB->OnItemAcquired.Broadcast(After, 1);
	InteractorB->OnInteractionRefused.Broadcast(FText::FromString(TEXT("UnitRefusedAfter")));
	TestNull(TEXT("no item notice after EndPlay"), Fixture.FindByKey(After->GetFName()));
	TestEqual(TEXT("no refusal notice after EndPlay"), Fixture.CountText(TEXT("UnitRefusedAfter")), 0);
	UVBTutorialComponent* TutorialB = UVBTutorialComponent::FindFor(StateB);
	if (TestNotNull(TEXT("tutorial B"), TutorialB))
	{
		CompleteTutorial(*this, *TutorialB, FText::FromString(TEXT("UnitTutorialAfter")));
		TestEqual(TEXT("no tutorial notice after EndPlay"), Fixture.CountText(TEXT("UnitTutorialAfter")), 0);
		TestEqual(TEXT("EndPlay keeps the tutorial queue for the next consumer"), TutorialB->TakeCompletionNotices().Num(), 1);
	}

	Fixture.Shutdown();
	return true;
}

#endif
