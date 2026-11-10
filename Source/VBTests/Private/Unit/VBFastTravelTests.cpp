// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Character/VBCharacter.h"
#include "Interaction/VBSavePoint.h"
#include "Map/VBFastTravelComponent.h"
#include "Map/VBPlayerRelocationComponent.h"
#include "Map/VBTravelPointSubsystem.h"
#include "Player/VBPlayerController.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Unit/VBPlayerRestoreTestAccess.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Blueprint/WidgetBlueprintGeneratedClass.h"
#include "Blueprint/WidgetTree.h"
#include "Interaction/VBInteractableConfig.h"
#include "UI/Map/VBFastTravelScreenWidget.h"
#include "UI/VBUISettings.h"
#include "Components/BoxComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/TargetPoint.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/PlayerState.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "Player/VBPlayerReadinessComponent.h"
#include "Player/VBPlayerRules.h"
#include "SaveSystem/VBSaveGameSubsystem.h"

// 빠른 이동: 저장점에서 연 목록 -> 서버 판정 -> 복원 컴포넌트의 순간 이동.
//   실제 RPC 진입점을 탄다(독립 실행 월드라 서버, 주인 클라 RPC 가 이 자리에서 돈다).

class FVBFastTravelTestAccess
{
public:
	static void SetFadeOut(UVBFastTravelComponent& Component, float Seconds) { Component.FadeOutSeconds = Seconds; }
	static void ReceiveDiscovered(UVBFastTravelComponent& Component, const TArray<FGameplayTag>& List) { Component.ClientSetDiscovered_Implementation(List); }
};

class FVBTravelPointTestSetter
{
public:
	static AVBSavePoint* SpawnPoint(UWorld& World, const TCHAR* Id, const FVector& Location)
	{
		AVBSavePoint* Point = World.SpawnActor<AVBSavePoint>(Location, FRotator::ZeroRotator);
		if (Point)
		{
			FProperty* Property = AVBSavePoint::StaticClass()->FindPropertyByName(TEXT("PointId"));
			if (FStructProperty* Struct = CastField<FStructProperty>(Property))
			{
				*Struct->ContainerPtrToValuePtr<FGameplayTag>(Point) = FGameplayTag::RequestGameplayTag(Id);
			}

			Point->DispatchBeginPlay();
		}

		return Point;
	}
};

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFastTravelServerTest, "VB.Unit.FastTravel.ServerTravel",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFastTravelServerTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld* World = Isolated.World;
	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(World);
	AVBPlayerController* PC = World->SpawnActor<AVBPlayerController>();
	AVBCharacter* Pawn = World->SpawnActor<AVBCharacter>(FVector::ZeroVector, FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("travel subsystem"), Travel) || !TestNotNull(TEXT("controller"), PC) || !TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}
	// C++ 몸에는 복원 컴포넌트가 없다(BP_VBCharacter 가 붙인다) - 시험이 필요한 것을 붙인다.
	UVBPlayerRestoreComponent* Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
	Pawn->AddInstanceComponent(Restore);
	Restore->RegisterComponent();
	PC->Possess(Pawn);
	UVBFastTravelComponent* FastTravel = PC->FindComponentByClass<UVBFastTravelComponent>();
	if (!TestNotNull(TEXT("fast travel component on the controller"), FastTravel)
		|| !TestNotNull(TEXT("restore component on the pawn"), Restore))
	{
		return false;
	}

	FVBFastTravelTestAccess::SetFadeOut(*FastTravel, 0.0f);

	AVBSavePoint* Here = FVBTravelPointTestSetter::SpawnPoint(*World, TEXT("Map.TravelPoint.Test.A"), FVector::ZeroVector);
	AVBSavePoint* There = FVBTravelPointTestSetter::SpawnPoint(*World, TEXT("Map.TravelPoint.Test.B"), FVector(5000.0f, 0.0f, 0.0f));
	if (!TestNotNull(TEXT("points"), Here) || !There)
	{
		return false;
	}

	const FGameplayTag HereId = Here->GetPointId();
	const FGameplayTag ThereId = There->GetPointId();

	// 발견하지 않은 지점은 목록에 없다.
	Travel->Discover(HereId);
	TestTrue(TEXT("opens at a registered save point"), FastTravel->OpenTravel(Here));
	TestEqual(TEXT("an undiscovered point is not listed"), FastTravel->GetView().Entries.Num(), 0);
	FastTravel->RequestClose();
	TestFalse(TEXT("closed on request"), FastTravel->IsTravelOpen());

	Travel->Discover(ThereId);
	TestTrue(TEXT("reopens"), FastTravel->OpenTravel(Here));
	TestEqual(TEXT("the other discovered point is listed, the current one is not"), FastTravel->GetView().Entries.Num(), 1);
	TestTrue(TEXT("listed entry is the destination"), FastTravel->GetView().Entries.Num() == 1
		&& FastTravel->GetView().Entries[0].PointId == ThereId);
	TestEqual(TEXT("preview: same point"), FastTravel->PreviewTravel(HereId), EVBFastTravelResult::SamePoint);

	// 거절은 폰을 옮기지 않는다.
	const FVector Start = Pawn->GetActorLocation();
	FastTravel->RequestTravel(HereId);
	FastTravel->RequestTravel(FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint")));
	Pawn->SetActorLocation(FVector(0.0f, 2000.0f, 0.0f));
	FastTravel->RequestTravel(ThereId);
	TestTrue(TEXT("refusals (same point, unknown, walked away) do not move the pawn"),
		Pawn->GetActorLocation().Equals(FVector(0.0f, 2000.0f, 0.0f), 1.0f));
	Pawn->SetActorLocation(Start);

	// 성공: 목록이 닫히고 폰이 도착 자리로 간다. 불러오기 영수증은 그대로다.
	const FVBPlayerRestoreReceipt ReceiptBefore = Restore->GetRestoreReceipt();
	FastTravel->RequestTravel(ThereId);
	TestFalse(TEXT("travel closes the list"), FastTravel->IsTravelOpen());
	const FVector Arrival = There->GetArrivalTransform().GetLocation();
	TestTrue(TEXT("pawn is at the arrival (XY)"), FVector::Dist2D(Pawn->GetActorLocation(), Arrival) < 1.0f);
	// 스트리밍 대기는 타이머다 - 공유 스케줄러를 돌리지 않고 실제 폴링을 한 번 부른다(월드 파티션이 없어 첫 폴링에서 끝난다).
	FVBPlayerRestoreTestAccess::Poll(*Restore);
	TestFalse(TEXT("travel finishes"), FastTravel->IsTravelling());
	TestFalse(TEXT("restore is no longer running"), Restore->IsRestoring());
	const FVBPlayerRestoreReceipt& ReceiptAfter = Restore->GetRestoreReceipt();
	TestTrue(TEXT("the load receipt is untouched by a travel"),
		ReceiptAfter.Outcome == ReceiptBefore.Outcome && ReceiptAfter.OperationId == ReceiptBefore.OperationId);
	return true;
}

// 원격 클라의 발견 사본: 서버 목록으로 갈아끼우고, 권한 쪽 PC 는 받은 목록을 버린다.
//   덮지 않는 것: 실제 원격 전송(BeginPlay 의 요청, 바뀜 때의 밀기) - 독립 실행 월드에는 연결이 없다. 네트워크 시험이 덮는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFastTravelClientDiscoveredTest, "VB.Unit.FastTravel.ClientDiscoveredSync",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFastTravelClientDiscoveredTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	UWorld* World = Isolated.World;
	UVBTravelPointSubsystem* Travel = UVBTravelPointSubsystem::Get(World);
	AVBPlayerController* HostPC = World->SpawnActor<AVBPlayerController>();
	AVBPlayerController* RemotePC = World->SpawnActor<AVBPlayerController>();
	if (!TestNotNull(TEXT("travel subsystem"), Travel) || !TestNotNull(TEXT("host controller"), HostPC)
		|| !TestNotNull(TEXT("remote controller"), RemotePC))
	{
		return false;
	}
	// 주인 클라에서 본 PC 의 역할. 권한이 없으면 받은 목록을 쓴다.
	RemotePC->SetRole(ROLE_AutonomousProxy);
	UVBFastTravelComponent* HostTravel = HostPC->FindComponentByClass<UVBFastTravelComponent>();
	UVBFastTravelComponent* RemoteTravel = RemotePC->FindComponentByClass<UVBFastTravelComponent>();
	if (!TestNotNull(TEXT("host fast travel"), HostTravel) || !TestNotNull(TEXT("remote fast travel"), RemoteTravel))
	{
		return false;
	}

	const FGameplayTag Ward = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.A"));
	const FGameplayTag Elsewhere = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint.Test.B"));
	const FGameplayTag Root = FGameplayTag::RequestGameplayTag(TEXT("Map.TravelPoint"));
	int32 Notices = 0;
	Travel->OnDiscoveredChanged.AddLambda([&Notices]() { ++Notices; });

	TestTrue(TEXT("server discovery"), Travel->Discover(Ward));
	TestEqual(TEXT("a new discovery notifies"), Notices, 1);

	// 버리는 두 개(빈 태그, 뿌리)는 말하고 버린다.
	AddExpectedMessagePlain(TEXT("서버가 보낸 지점 ID"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	Travel->ApplyServerDiscovered({Elsewhere, FGameplayTag(), Root, Elsewhere});
	TestEqual(TEXT("apply replaces the list and drops empty, root and duplicate ids"), Travel->GetDiscovered().Num(), 1);
	TestTrue(TEXT("applied id"), Travel->IsDiscovered(Elsewhere));
	TestFalse(TEXT("the old list is gone"), Travel->IsDiscovered(Ward));
	TestEqual(TEXT("apply notifies"), Notices, 2);

	FVBFastTravelTestAccess::ReceiveDiscovered(*HostTravel, {Ward});
	TestFalse(TEXT("an authority owner ignores a received list"), Travel->IsDiscovered(Ward));
	TestEqual(TEXT("ignored list does not notify"), Notices, 2);

	FVBFastTravelTestAccess::ReceiveDiscovered(*RemoteTravel, {Ward, Elsewhere});
	TestTrue(TEXT("a remote owner applies a received list"), Travel->IsDiscovered(Ward) && Travel->IsDiscovered(Elsewhere));
	TestEqual(TEXT("received list notifies"), Notices, 3);
	return true;
}

// 실제 자산 배선: 화면 태그가 GameMenu 층의 WBP_FastTravel 로 가고, 저장점의 이동 상호작용 설정이 누름이다.
//   끊기면 목록이 안 뜨거나 다가가기만 해도 열린다 - 그래서 실제 자산으로 잠근다. 맵에 배치된 저장점은 여기서 안 본다.
//   설정 자산은 배치마다 TravelInteractable 에 꽂아서(설정에서 찾는 길이 없다) 경로로 연다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBFastTravelAssetsTest, "VB.Unit.FastTravel.Assets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBFastTravelAssetsTest::RunTest(const FString& Parameters)
{
	const FVBScreenRoute* Route = GetDefault<UVBUISettings>()->Screens.Find(VBGameplayTags::UI_Screen_FastTravel);
	if (!TestNotNull(TEXT("fast travel screen has a route (DefaultGame.ini Screens)"), Route))
	{
		return false;
	}

	const FGameplayTag GameMenu = FGameplayTag::RequestGameplayTag(TEXT("UI.Layer.GameMenu"), false);
	TestTrue(TEXT("GameMenu layer tag exists (else an unset layer would compare equal)"), GameMenu.IsValid());
	TestTrue(TEXT("route layer is GameMenu"), GameMenu.IsValid() && Route->Layer == GameMenu);
	UClass* ScreenClass = Route->ScreenClass.LoadSynchronous();
	if (!TestNotNull(TEXT("route class loads"), ScreenClass))
	{
		return false;
	}

	TestTrue(TEXT("route class is the fast travel screen"), ScreenClass->IsChildOf(UVBFastTravelScreenWidget::StaticClass()));
	const FClassProperty* RowClass = FindFProperty<FClassProperty>(UVBFastTravelScreenWidget::StaticClass(), TEXT("EntryWidgetClass"));
	TestTrue(TEXT("WBP sets EntryWidgetClass"), RowClass
		&& RowClass->GetPropertyValue_InContainer(GetDefault<UObject>(ScreenClass)) != nullptr);
	const UWidgetBlueprintGeneratedClass* WidgetClass = Cast<UWidgetBlueprintGeneratedClass>(ScreenClass);
	const UWidgetTree* Tree = WidgetClass ? WidgetClass->GetWidgetTreeArchetype() : nullptr;
	for (const TCHAR* Name : {TEXT("EntryBox"), TEXT("BtnTravel"), TEXT("TxtStatus")})
	{
		TestTrue(FString::Printf(TEXT("WBP has %s"), Name), Tree && Tree->FindWidget(Name) != nullptr);
	}

	const UVBInteractableConfig* Config = LoadObject<UVBInteractableConfig>(nullptr,
		TEXT("/Game/VB/Data/Interaction/DA_Interactable_FastTravel.DA_Interactable_FastTravel"));
	if (!TestNotNull(TEXT("travel interact config loads"), Config))
	{
		return false;
	}

	TestTrue(TEXT("travel interaction is a press, not automatic"), Config->Mode == EVBInteractMode::Tap);
	TestTrue(TEXT("travel interaction has a prompt"), !Config->ActionText.IsEmpty());
	return true;
}

// 자리로 옮기기: 붙들기 -> 옮기기 -> 도착 검사 -> 도착 효과 한 번. 도착하지 못하면 붙든 자리로 돌아가고 효과는 없다.
//   물리 장면이 있는 월드라 바닥 트레이스, 설 공간 검사가 실제로 돈다. 타이머는 돌리지 않고 붙들기 끝, 폴링, 지키미를 직접 부른다.
//   덮지 않는 것: 트래버설, 루트 모션 중 거절(트래버설 상태를 시험에서 켤 길이 없다), 원격 클라의 페이드 - PIE 가 덮는다.
namespace VBRelocationTest
{
	const FVector Falling(0.0, 0.0, 1000.0);
	const FVector Arrival(3000.0, 0.0, 0.0);

	struct FWorld
	{
		FVBIsolatedGameWorld Isolated;
		UWorld* World = nullptr;
		AVBPlayerController* PC = nullptr;
		AVBCharacter* Pawn = nullptr;
		UVBPlayerRelocationComponent* Mover = nullptr;
		UVBPlayerRestoreComponent* Restore = nullptr;
		UVBStoryStateComponent* Story = nullptr;
		FGameplayTag Counter;

		bool Init(FAutomationTestBase& Test, bool bFloorAtArrival)
		{
			if (!Isolated.Init(Test, /*bPhysicsScene=*/true)) return false;
			World = Isolated.World;

			AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
			if (!Test.TestNotNull(TEXT("game state"), GameState))
			{
				return false;
			}

			World->SetGameState(GameState);
			Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();

			FActorSpawnParameters Spawn;
			Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
			PC = World->SpawnActor<AVBPlayerController>();
			Pawn = World->SpawnActor<AVBCharacter>(Falling, FRotator::ZeroRotator, Spawn);
			if (!Test.TestNotNull(TEXT("controller"), PC) || !Test.TestNotNull(TEXT("pawn"), Pawn))
			{
				return false;
			}

			if (!PC->PlayerState)
			{
				// 게임 모드가 없어 PlayerState 가 안 생긴다. 주인을 PC 로 둔다 - 효과가 PlayerState -> GetPlayerController(주인)로 PC 를 찾는다.
				FActorSpawnParameters Owned;
				Owned.Owner = PC;
				PC->PlayerState = World->SpawnActor<APlayerState>(Owned);
			}
			// C++ 몸에는 복원 컴포넌트가 없다(BP_VBCharacter 가 붙인다) - 시험이 필요한 것을 붙인다.
			Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
			Pawn->AddInstanceComponent(Restore);
			Restore->RegisterComponent();
			PC->Possess(Pawn);
			Mover = PC->FindComponentByClass<UVBPlayerRelocationComponent>();
			if (!Test.TestNotNull(TEXT("mover on the controller"), Mover) || !Test.TestNotNull(TEXT("restore on the pawn"), Restore))
			{
				return false;
			}

			if (!Test.TestTrue(TEXT("player state resolves its controller"), PC->PlayerState
				&& PC->PlayerState->GetPlayerController() == PC))
			{
				return false;
			}

			if (bFloorAtArrival)
			{
				if (!SpawnBox(Arrival - FVector(0.0, 0.0, 10.0), FVector(400.0, 400.0, 10.0)))
				{
					return false;
				}
				// 전제: 틱 없는 물리 장면에서도 새 바닥이 질의에 보인다(PlayerPreparation.GroundRestore 와 같은 조건).
				FHitResult Hit;
				if (!Test.TestTrue(TEXT("precondition: the floor answers a trace"),
					World->LineTraceSingleByChannel(Hit, Arrival + FVector(0.0, 0.0, 300.0), Arrival - FVector(0.0, 0.0, 100.0),
						ECC_WorldStatic)))
				{
					return false;
				}
			}

			Counter = FGameplayTag::RequestGameplayTag(TEXT("Story.Test.Talked"));
			return true;
		}

		AActor* SpawnBox(const FVector& Center, const FVector& Extent) const
		{
			AActor* Actor = World->SpawnActor<AActor>();
			if (!Actor)
			{
				return nullptr;
			}

			UBoxComponent* Box = NewObject<UBoxComponent>(Actor);
			Actor->AddInstanceComponent(Box);
			Actor->SetRootComponent(Box);
			Box->SetMobility(EComponentMobility::Static);
			Box->SetBoxExtent(Extent);
			Box->SetCollisionObjectType(ECC_WorldStatic);
			Box->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
			Box->SetCollisionResponseToAllChannels(ECR_Block);
			Actor->SetActorLocation(Center);
			Box->RegisterComponent();
			return Actor;
		}

		// 표지 자리: 바닥(윗면 Z0) 위 100 - 복원이 지면으로 내려 붙인다.
		static FTransform ArrivalTransform() { return FTransform(FRotator(0.0f, 90.0f, 0.0f), Arrival + FVector(0.0, 0.0, 100.0)); }

		TArray<FInstancedStruct> CountOnArrival() const
		{
			FVBEffect_AddCounter Add;
			Add.Counter = Counter;
			return {FInstancedStruct::Make(Add)};
		}

		bool Start() const { return Mover->StartScriptedMove(ArrivalTransform(), 0.2f, 0.4f, CountOnArrival()); }
		bool IsFrozen() const { return Pawn->GetCharacterMovement()->MovementMode == MOVE_None; }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScriptedMoveArrivesTest, "VB.Unit.FastTravel.ScriptedMove.Arrives",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScriptedMoveArrivesTest::RunTest(const FString& Parameters)
{
	VBRelocationTest::FWorld F;
	if (!F.Init(*this, /*bFloorAtArrival=*/true)) return false;

	TestTrue(TEXT("accepted"), F.Start());
	TestTrue(TEXT("the one relocation owner is busy"), F.Mover->IsRelocating() && F.Restore->IsRestoring());
	TestTrue(TEXT("hold runs for the fade"), FVBPlayerRestoreTestAccess::IsHoldPending(*F.Restore));
	TestTrue(TEXT("held where it was during the fade (does not keep falling)"),
		F.Pawn->GetActorLocation().Equals(VBRelocationTest::Falling, 1.0));
	TestTrue(TEXT("frozen during the hold"), F.IsFrozen());
	TestFalse(TEXT("a second request while moving is refused"), F.Start());

	// 늦게 온 남의 알림은 이 이동을 끝내지 않는다.
	F.Restore->OnTeleportFinished.Broadcast(FGuid::NewGuid(), EVBPlayerRestoreOutcome::Completed);
	TestTrue(TEXT("a foreign operation id is ignored"), F.Mover->IsRelocating());
	TestEqual(TEXT("a foreign notice applies nothing"), F.Story->GetCounter(F.Counter), 0);

	FVBPlayerRestoreTestAccess::EndHold(*F.Restore);
	TestTrue(TEXT("placed at the arrival after the hold"), FVector::Dist2D(F.Pawn->GetActorLocation(), VBRelocationTest::Arrival) < 1.0);
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	// 지면 스냅은 바닥 + 반높이에 놓고, 걷기로 돌아갈 때 CMC 가 바닥 간격(1.9~2.4cm)만큼 올린다 - 그 범위를 허용한다.
	const double HalfHeight = F.Pawn->GetCapsuleComponent()->GetScaledCapsuleHalfHeight();
	const double StandZ = F.Pawn->GetActorLocation().Z;
	TestTrue(TEXT("stands on the floor at the arrival"), StandZ >= HalfHeight - 0.5 && StandZ <= HalfHeight + 3.0);
	TestTrue(TEXT("faces the marker yaw"), FMath::IsNearlyEqual(F.Pawn->GetActorRotation().Yaw, 90.0, 0.5));
	TestFalse(TEXT("move finished"), F.Mover->IsRelocating());
	TestFalse(TEXT("unfrozen"), F.IsFrozen());
	TestEqual(TEXT("arrival effects applied once"), F.Story->GetCounter(F.Counter), 1);

	// 끝난 뒤 남의 알림이 와도 효과는 다시 걸리지 않는다.
	F.Restore->OnTeleportFinished.Broadcast(FGuid::NewGuid(), EVBPlayerRestoreOutcome::Completed);
	TestEqual(TEXT("no second application"), F.Story->GetCounter(F.Counter), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScriptedMoveFailsTest, "VB.Unit.FastTravel.ScriptedMove.FailureReturnsToHold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScriptedMoveFailsTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("얼린 자리"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	{
		// 바닥이 없는 표지: 떨어지던 자리로 돌아가고(구덩이 바닥으로 계속 떨어진다, user [g]) 효과는 없다.
		VBRelocationTest::FWorld F;
		if (!F.Init(*this, /*bFloorAtArrival=*/false)) return false;
		TestTrue(TEXT("accepted"), F.Start());
		FVBPlayerRestoreTestAccess::EndHold(*F.Restore);
		FVBPlayerRestoreTestAccess::Poll(*F.Restore);
		TestTrue(TEXT("no floor: back where it was held"), F.Pawn->GetActorLocation().Equals(VBRelocationTest::Falling, 1.0));
		TestFalse(TEXT("no floor: move finished"), F.Mover->IsRelocating() || F.Restore->IsRestoring());
		TestFalse(TEXT("no floor: unfrozen (falls on)"), F.IsFrozen());
		TestEqual(TEXT("no floor: no arrival effects"), F.Story->GetCounter(F.Counter), 0);
	}

	{
		// 바닥은 있지만 설 자리를 벽이 먹는다(트레이스 선은 비켜 간다): 막힘 -> 돌아간다.
		VBRelocationTest::FWorld F;
		if (!F.Init(*this, /*bFloorAtArrival=*/true)) return false;
		// 벽은 트레이스 선에서 20cm 떨어진다 - 캡슐 반지름이 그보다 커야 설 자리를 먹는다.
		if (!TestTrue(TEXT("precondition: capsule radius reaches the wall"),
			F.Pawn->GetCapsuleComponent()->GetScaledCapsuleRadius() > 20.0f))
		{
			return false;
		}

		if (!TestNotNull(TEXT("blocking wall"),
			F.SpawnBox(VBRelocationTest::Arrival + FVector(30.0, 0.0, 100.0), FVector(10.0, 50.0, 80.0))))
		{
			return false;
		}

		TestTrue(TEXT("accepted"), F.Start());
		FVBPlayerRestoreTestAccess::EndHold(*F.Restore);
		FVBPlayerRestoreTestAccess::Poll(*F.Restore);
		TestTrue(TEXT("obstructed: back where it was held"), F.Pawn->GetActorLocation().Equals(VBRelocationTest::Falling, 1.0));
		TestEqual(TEXT("obstructed: no arrival effects"), F.Story->GetCounter(F.Counter), 0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScriptedMoveRefusalTest, "VB.Unit.FastTravel.ScriptedMove.Refusals",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScriptedMoveRefusalTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("도착 표지가 비었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	VBRelocationTest::FWorld F;
	if (!F.Init(*this, /*bFloorAtArrival=*/true)) return false;

	// 복원 컴포넌트가 다른 작업 중이면 거절하고 그 작업을 건드리지 않는다.
	FVBTeleportRequest Other;
	Other.Target = FTransform(FVector(-500.0, 0.0, 1000.0));
	Other.HoldSeconds = 1.0f;
	const FGuid OtherId = F.Restore->BeginTeleport(Other);
	TestTrue(TEXT("another teleport is running"), OtherId.IsValid());
	TestFalse(TEXT("refused while the restore component is busy"), F.Start());
	TestFalse(TEXT("the refusal did not take ownership"), F.Mover->IsRelocating());
	TestTrue(TEXT("the other operation still runs"), F.Restore->IsRestoring() && FVBPlayerRestoreTestAccess::IsHoldPending(*F.Restore));
	F.Restore->CancelTeleport(OtherId);
	TestFalse(TEXT("cancelled by its own id"), F.Restore->IsRestoring());

	// 새 게임 초기화(시네마틱 시작 보류가 걸리는 구간)에는 거절한다 - 감독이 요청을 받기 전부터 걸리는 보류다.
	UVBPlayerReadinessComponent* Readiness = F.Pawn->FindComponentByClass<UVBPlayerReadinessComponent>();
	if (!TestNotNull(TEXT("readiness on the possessed pawn"), Readiness))
	{
		return false;
	}

	Readiness->BeginInitialization(FGuid::NewGuid(), EVBGameStartIntent::NewGame);
	TestFalse(TEXT("refused during initialization"), F.Start());
	TestTrue(TEXT("refusal leaves the pawn alone"), F.Pawn->GetActorLocation().Equals(VBRelocationTest::Falling, 1.0) && !F.IsFrozen());

	// 효과: 표지가 비면 옮기지 않는다(대신 갈 자리를 찾지 않는다).
	const FVBRuleContext Context = FVBRuleContext::Make(F.World, F.PC->PlayerState);
	FVBEffect_MoveToPoint Empty;
	Empty.Apply(Context);
	TestFalse(TEXT("an empty marker moves nothing"), F.Mover->IsRelocating());
	return true;
}

// 옮기기는 FastTravel 을 모른다 - 시작 알림으로 열린 목록이 닫힌다. 거절은 목록을 건드리지 않는다.
//   대조: UVBFastTravelComponent::InitializeComponent 의 구독을 빼면 마지막 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScriptedMoveClosesListTest, "VB.Unit.FastTravel.ScriptedMove.ClosesOpenList",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScriptedMoveClosesListTest::RunTest(const FString& Parameters)
{
	VBRelocationTest::FWorld F;
	if (!F.Init(*this, /*bFloorAtArrival=*/true)) return false;

	UVBFastTravelComponent* FastTravel = F.PC->FindComponentByClass<UVBFastTravelComponent>();
	AVBSavePoint* Here = FVBTravelPointTestSetter::SpawnPoint(*F.World, TEXT("Map.TravelPoint.Test.A"), VBRelocationTest::Falling);
	if (!TestNotNull(TEXT("fast travel"), FastTravel) || !TestNotNull(TEXT("save point"), Here))
	{
		return false;
	}

	TestTrue(TEXT("the fade RPC component replicates"), F.Mover->GetIsReplicated());
	TestTrue(TEXT("list opens"), FastTravel->OpenTravel(Here));
	TestFalse(TEXT("a bad fade time is refused"), F.Mover->StartScriptedMove(VBRelocationTest::FWorld::ArrivalTransform(), -1.0f, 0.4f, {}));
	TestTrue(TEXT("a refusal leaves the list open"), FastTravel->IsTravelOpen());
	TestTrue(TEXT("accepted"), F.Start());
	TestTrue(TEXT("the session sees the relocation"), FastTravel->IsTravelling());
	TestFalse(TEXT("an accepted move closes the list"), FastTravel->IsTravelOpen());
	FVBPlayerRestoreTestAccess::EndHold(*F.Restore);
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	TestFalse(TEXT("the session sees the end of the relocation"), FastTravel->IsTravelling());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBScriptedMoveOwnershipTest, "VB.Unit.FastTravel.ScriptedMove.Ownership",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBScriptedMoveOwnershipTest::RunTest(const FString& Parameters)
{
	AddExpectedMessagePlain(TEXT("도착 알림 없이"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	AddExpectedMessagePlain(TEXT("플레이어/폰이 바뀌었다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	VBRelocationTest::FWorld F;
	if (!F.Init(*this, /*bFloorAtArrival=*/true)) return false;

	// 효과로 시작한다. 표지가 기울어 있어도 야우만 쓴다.
	ATargetPoint* Marker = F.World->SpawnActor<ATargetPoint>(VBRelocationTest::Arrival + FVector(0.0, 0.0, 100.0),
		FRotator(30.0f, 90.0f, 0.0f));
	if (!TestNotNull(TEXT("marker"), Marker))
	{
		return false;
	}

	FVBEffect_MoveToPoint Move;
	Move.Destination = Marker;
	Move.OnArrivedEffects = F.CountOnArrival();
	Move.Apply(FVBRuleContext::Make(F.World, F.PC->PlayerState));
	TestTrue(TEXT("the effect started a move"), F.Mover->IsRelocating());

	// 지키미: 제 작업만 끝내고 붙든 폰을 돌려놓는다. 효과는 없다.
	F.Mover->AbortRelocation();
	TestFalse(TEXT("watchdog ends the move"), F.Mover->IsRelocating() || F.Restore->IsRestoring());
	TestTrue(TEXT("watchdog: back where it was held"), F.Pawn->GetActorLocation().Equals(VBRelocationTest::Falling, 1.0));
	TestFalse(TEXT("watchdog: unfrozen"), F.IsFrozen());
	TestEqual(TEXT("watchdog: no arrival effects"), F.Story->GetCounter(F.Counter), 0);

	// 붙드는 사이 폰이 바뀌면: 옛 폰은 제 작업대로 옮겨지고, 새 폰은 그대로며, 옛 요청의 효과는 새 폰에 걸리지 않는다.
	TestTrue(TEXT("accepted again"), F.Start());
	FActorSpawnParameters Spawn;
	Spawn.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;
	const FVector ReplacementSpot(-2000.0, 0.0, 500.0);
	AVBCharacter* Replacement = F.World->SpawnActor<AVBCharacter>(ReplacementSpot, FRotator::ZeroRotator, Spawn);
	if (!TestNotNull(TEXT("replacement pawn"), Replacement))
	{
		return false;
	}

	F.PC->Possess(Replacement);
	FVBPlayerRestoreTestAccess::EndHold(*F.Restore);
	FVBPlayerRestoreTestAccess::Poll(*F.Restore);
	TestTrue(TEXT("the original pawn finished its own move"), FVector::Dist2D(F.Pawn->GetActorLocation(), VBRelocationTest::Arrival) < 1.0);
	TestTrue(TEXT("the replacement pawn was not moved"), Replacement->GetActorLocation().Equals(ReplacementSpot, 1.0));
	TestFalse(TEXT("move finished"), F.Mover->IsRelocating());
	TestEqual(TEXT("no effects for a replaced pawn"), F.Story->GetCounter(F.Counter), 0);
	return true;
}
#endif
