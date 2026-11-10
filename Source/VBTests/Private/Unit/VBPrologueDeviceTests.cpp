#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "SaveSystem/VBPlayerRestoreComponent.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "SaveSystem/VBSaveParticipant.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Enemy/VBEncounterGateComponent.h"
#include "Enemy/VBEncounterRules.h"
#include "Animation/AnimSequence.h"
#include "Misc/DataValidation.h"
#include "UObject/Package.h"
#include "Interaction/VBInteractableComponent.h"
#include "Narrative/Story/VBRuleTypes.h"
#include "Narrative/Story/VBStoryStateComponent.h"
#include "GameFramework/GameStateBase.h"
#include "GameFramework/WorldSettings.h"
#include "Kismet/GameplayStatics.h"
#include "Unit/VBIsolatedGameWorld.h"
#include "Unit/VBStoryTestWorld.h"
#include "Narrative/Cinematic/VBCinematicRules.h"
#include "Narrative/Story/VBAreaTrigger.h"

// 프롤로그 첫 구간 장치 둘의 단위 시험. 줄 보이는 시간(지정값 우선, 아니면 최소+글자당)과
// 체크포인트가 '상태는 지금, 자리는 지정한 곳' 으로 쓰이는가를 잰다. RPC, 자막 표시, 실제 슬롯 저장은 PIE 가 본다.

#if WITH_AUTOMATION_TESTS
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBSpokenLineSecondsTest, "VB.Unit.Narrative.SpokenLineSeconds",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBSpokenLineSecondsTest::RunTest(const FString& Parameters)
{
	const UVBSpokenLineComponent* Lines = GetDefault<UVBSpokenLineComponent>();
	FVBSpokenLine Line;
	Line.Text = FText::FromString(TEXT("저기요."));
	TestEqual(TEXT("short line uses the minimum"), Lines->ResolveSeconds(Line), Lines->MinLineSeconds);

	Line.Text = FText::FromString(FString::ChrN(100, TEXT('가')));
	TestEqual(TEXT("long line scales with characters"), Lines->ResolveSeconds(Line), 100 * Lines->SecondsPerCharacter);

	Line.Duration = 0.5f;
	TestEqual(TEXT("authored duration wins"), Lines->ResolveSeconds(Line), 0.5f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCheckpointRestoreAtTest, "VB.Unit.Save.CheckpointRestoreAt",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCheckpointRestoreAtTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACharacter* Pawn = World->SpawnActor<ACharacter>(FVector(100.0f, 200.0f, 300.0f), FRotator::ZeroRotator);
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	UVBPlayerRestoreComponent* Restore = NewObject<UVBPlayerRestoreComponent>(Pawn);
	Pawn->AddInstanceComponent(Restore);
	Restore->RegisterComponent();

	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	FVBSaveContext Context;
	Context.World = World;
	Context.Pawn = Pawn;

	// 보통 저장: 지금 자리.
	Restore->WriteSave(*Save, Context);
	TestTrue(TEXT("plain save writes a transform"), Save->Progress.bHasSavedTransform);
	TestTrue(TEXT("plain save uses where the pawn stands"),
		Save->Progress.SavedPlayerTransform.GetLocation().Equals(Pawn->GetActorLocation(), 0.01));

	// 체크포인트: 지정한 자리.
	const FTransform Marker(FRotator(0.0f, 90.0f, 0.0f), FVector(1850.0f, 1300.0f, 90.0f));
	Context.RestoreTransformOverride = Marker;
	Restore->WriteSave(*Save, Context);
	TestTrue(TEXT("checkpoint uses the marker location"),
		Save->Progress.SavedPlayerTransform.GetLocation().Equals(Marker.GetLocation(), 0.01));
	TestTrue(TEXT("checkpoint uses the marker yaw"),
		FMath::IsNearlyEqual(Save->Progress.SavedPlayerTransform.Rotator().Yaw, 90.0, 0.01));
	return true;
}

// 체크포인트 슬롯(f3 검증 10-07): 활성 슬롯 -> 빈 슬롯 -> 둘 다 없으면 거절. 만석일 때 0번을 덮지 않는 것이 요점이다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCheckpointSlotTest, "VB.Unit.Save.CheckpointSlot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCheckpointSlotTest::RunTest(const FString& Parameters)
{
	auto Slot = [](int32 Index, bool bExists)
	{
		FVBSaveSlotMeta Meta;
		Meta.SlotIndex = Index;
		Meta.bExists = bExists;
		return Meta;
	};
	const TArray<FVBSaveSlotMeta> Full = { Slot(0, true), Slot(1, true), Slot(2, true) };
	const TArray<FVBSaveSlotMeta> OneFree = { Slot(0, true), Slot(1, false), Slot(2, true) };
	TestEqual(TEXT("active slot wins even when full"), VBSaveSlotPolicy::ResolveCheckpointSlot(2, Full), 2);
	TestEqual(TEXT("no active slot -> first empty slot"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, OneFree), 1);
	TestEqual(TEXT("no active slot and full -> refuse, not slot 0"), VBSaveSlotPolicy::ResolveCheckpointSlot(INDEX_NONE, Full),
		(int32)INDEX_NONE);
	return true;
}

// 조우 보류: 꼬리표가 맞는 조우 문만 보류되고, 같은 값은 다시 알리지 않는다.
// 행동 트리 멈춤(AI 컨트롤러)과 공격 GA 막기(로스태그)는 ASC, 컨트롤러가 있는 PIE 에서 본다 - 여기 액터는 ASC 가 없어 경고를 낸다.

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterHoldTest, "VB.Unit.Encounter.Hold",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterHoldTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	// 꼬리표 칸은 배치에서 적는 protected 칸이라 리플렉션으로 채운다. 태그 이름은 연산과 무관해 네이티브 태그를 빌린다.
	auto MakeGate = [World](FGameplayTag Tag) -> UVBEncounterGateComponent*
	{
		ACharacter* Pawn = World->SpawnActor<ACharacter>();
		UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(Pawn);
		Pawn->AddInstanceComponent(Gate);
		Gate->RegisterComponent();
		const FStructProperty* Prop = FindFProperty<FStructProperty>(UVBEncounterGateComponent::StaticClass(), TEXT("EncounterTags"));
		Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(Gate)->AddTag(Tag);
		return Gate;
	};
	UVBEncounterGateComponent* First = MakeGate(VBGameplayTags::State_Encounter_Held);
	UVBEncounterGateComponent* Other = MakeGate(VBGameplayTags::State_Dead);
	if (!TestNotNull(TEXT("gates"), First) || !TestNotNull(TEXT("gates"), Other))
	{
		return false;
	}

	int32 Broadcasts = 0;
	First->OnHeldChanged.AddLambda([&Broadcasts](bool) { ++Broadcasts; });

	FVBRuleContext Context;
	Context.World = World;
	FVBEffect_HoldEncounter Hold;
	Hold.EncounterTags.AddTag(VBGameplayTags::State_Encounter_Held);
	Hold.bHold = true;
	Hold.Apply(Context);
	TestTrue(TEXT("matching gate is held"), First->IsHeld());
	TestFalse(TEXT("other tag is untouched"), Other->IsHeld());
	TestEqual(TEXT("one broadcast"), Broadcasts, 1);

	Hold.Apply(Context);
	TestEqual(TEXT("same value does not broadcast again"), Broadcasts, 1);

	Hold.bHold = false;
	Hold.Apply(Context);
	TestFalse(TEXT("release"), First->IsHeld());
	TestEqual(TEXT("release broadcasts"), Broadcasts, 2);
	return true;
}

// 쓰러진 조우의 보류(Codex 03 지적 10-04): 새 보류는 안 걸고, 쓰러지기 전 걸린 보류는 걷되 방송하지 않는다(죽은 적의 행동 트리를 안 잇는다).
// 진행 중 공격 취소(CancelAbilities)는 ASC 가 있는 적이 필요해 PIE 확인 목록으로 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterHoldDefeatedTest, "VB.Unit.Encounter.HoldDefeated",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterHoldDefeatedTest::RunTest(const FString& Parameters)
{
	using VBEncounterHold::Decide;
	TestTrue(TEXT("alive: hold applies and notifies"), Decide(false, true, false) == EVBEncounterHoldAction::ApplyAndNotify);
	TestTrue(TEXT("alive: release applies and notifies"), Decide(true, false, false) == EVBEncounterHoldAction::ApplyAndNotify);
	TestTrue(TEXT("same value is ignored"), Decide(true, true, false) == EVBEncounterHoldAction::Ignore);
	TestTrue(TEXT("defeated: new hold is ignored"), Decide(false, true, true) == EVBEncounterHoldAction::Ignore);
	TestTrue(TEXT("defeated: release is silent"), Decide(true, false, true) == EVBEncounterHoldAction::ApplySilently);
	TestTrue(TEXT("defeated: same value is ignored"), Decide(false, false, true) == EVBEncounterHoldAction::Ignore);
	return true;
}

// 잠든 자세: 위에서부터 처음 참인 줄, 없으면 자세 없음(애님 BP 대기 그대로).
//   클립 없는 줄은 데이터 오류다. 실제 몽타주 재생, 장면 뒤 다시 걸림, 깨어나면 멈춤은 PIE 확인 목록으로 간다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterDormantPoseTest, "VB.Unit.Encounter.DormantPose",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterDormantPoseTest::RunTest(const FString& Parameters)
{
	TArray<bool> Rows;
	auto Pick = [&Rows]() { return VBEncounterDormant::Choose(Rows.Num(), [&Rows](int32 Index) { return Rows[Index]; }); };
	Rows = {false, true, true};
	TestEqual(TEXT("the first true row wins over later true rows"), Pick(), 1);
	Rows = {true, true};
	TestEqual(TEXT("list order is the priority"), Pick(), 0);
	Rows = {false, false};
	TestEqual(TEXT("no true row gives no pose"), Pick(), static_cast<int32>(INDEX_NONE));
	Rows.Reset();
	TestEqual(TEXT("an empty list gives no pose"), Pick(), static_cast<int32>(INDEX_NONE));

#if WITH_EDITOR
	UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(GetTransientPackage());
	FArrayProperty* PosesProperty = FindFProperty<FArrayProperty>(UVBEncounterGateComponent::StaticClass(), TEXT("DormantPoses"));
	if (!TestNotNull(TEXT("DormantPoses property"), PosesProperty))
	{
		return false;
	}

	TArray<FVBEncounterDormantPose>& Poses = *PosesProperty->ContainerPtrToValuePtr<TArray<FVBEncounterDormantPose>>(Gate);
	Poses.AddDefaulted();
	FDataValidationContext MissingClip;
	TestTrue(TEXT("a row without a clip is invalid"), Gate->IsDataValid(MissingClip) == EDataValidationResult::Invalid);
	Poses[0].Animation = NewObject<UAnimSequence>(GetTransientPackage());
	FDataValidationContext WithClip;
	TestTrue(TEXT("a row with a clip passes"), Gate->IsDataValid(WithClip) != EDataValidationResult::Invalid);
#endif
	return true;
}

// 상호작용 조건 칸: 비면 쓸 수 있고, 거짓이면 완료(NotifyInteracted)도 안 난다. 실제 사실 저장소를 세워
// 참, 거짓, 시작 뒤 거짓 전환과, 거절된 완료가 완료 효과를 안 돌리는지를 표지 사실로 잰다.
// GA 의 시작 거절, 클라 알림, HUD 한 줄은 PIE 가 본다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBInteractUseConditionsTest, "VB.Unit.Interaction.UseConditions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBInteractUseConditionsTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Initialization = UWorld::InitializationValues().AllowAudioPlayback(false)
		.CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Initialization);
	if (!TestNotNull(TEXT("test world"), World))
	{
		return false;
	}

	ON_SCOPE_EXIT { World->DestroyWorld(false); };

	ACharacter* Owner = World->SpawnActor<ACharacter>();
	UVBInteractableComponent* Target = NewObject<UVBInteractableComponent>(Owner);
	Owner->AddInstanceComponent(Target);
	Target->RegisterComponent();

	TestTrue(TEXT("no conditions: usable"), Target->PassesUseConditions(nullptr));
	TestTrue(TEXT("no conditions: completes"), Target->NotifyInteracted(nullptr));

	// 조건, 완료 효과 칸은 배치에서 적는 protected 칸이라 리플렉션으로 채운다. 사실 태그는 연산과 무관해 네이티브 태그를 빌린다.
	auto ArrayOf = [Target](const TCHAR* Name) -> TArray<FInstancedStruct>*
	{
		const FArrayProperty* Prop = FindFProperty<FArrayProperty>(UVBInteractableComponent::StaticClass(), Name);
		return Prop ? Prop->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(Target) : nullptr;
	};
	TArray<FInstancedStruct>* Conditions = ArrayOf(TEXT("UseConditions"));
	TArray<FInstancedStruct>* Effects = ArrayOf(TEXT("CompletionEffects"));
	if (!TestNotNull(TEXT("UseConditions property"), Conditions) || !TestNotNull(TEXT("CompletionEffects property"), Effects))
	{
		return false;
	}

	const FGameplayTag GateFact = VBGameplayTags::State_Encounter_Held; // 조건이 읽는 사실
	const FGameplayTag DoneMarker = VBGameplayTags::State_Dead; // 완료 효과가 켜는 표지 - 켜졌으면 효과가 돈 것이다
	FVBCondition_Fact Fact;
	Fact.Fact = GateFact;
	Conditions->Add(FInstancedStruct::Make(Fact));
	FVBEffect_SetFact Marker;
	Marker.Fact = DoneMarker;
	Effects->Add(FInstancedStruct::Make(Marker));

	// 저장소가 없으면 거짓(알 수 없는 것을 참으로 흘리지 않는다). 경고 둘.
	AddExpectedMessagePlain(TEXT("사실 저장소가 없다"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
	TestFalse(TEXT("no story: not usable"), Target->PassesUseConditions(nullptr));
	TestFalse(TEXT("no story: completion refused"), Target->NotifyInteracted(nullptr));

	// 실제 사실 저장소 - 게임 상태 컴포넌트(UVBStoryStateComponent::Get 이 찾는 자리).
	AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
	UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>(GameState);
	GameState->AddInstanceComponent(Story);
	Story->RegisterComponent();
	World->SetGameState(GameState);
	if (!TestTrue(TEXT("story is found through the world"), UVBStoryStateComponent::Get(Target) == Story))
	{
		return false;
	}

	TestFalse(TEXT("fact off: not usable"), Target->PassesUseConditions(nullptr));
	TestFalse(TEXT("fact off: completion refused"), Target->NotifyInteracted(nullptr));
	TestFalse(TEXT("fact off: completion effects did not run"), Story->HasFact(DoneMarker));

	Story->SetFact(GateFact, true);
	TestTrue(TEXT("fact on: usable"), Target->PassesUseConditions(nullptr));
	TestTrue(TEXT("fact on: completes"), Target->NotifyInteracted(nullptr));
	TestTrue(TEXT("fact on: completion effects ran"), Story->HasFact(DoneMarker));

	// 시작 때 참이었다가 완료 전에 거짓이 된 경우(동작 도중) - 완료 직전 재검사가 막는다.
	Story->SetFact(DoneMarker, false);
	TestTrue(TEXT("flip: usable at start"), Target->PassesUseConditions(nullptr));
	Story->SetFact(GateFact, false);
	TestFalse(TEXT("flip: completion refused"), Target->NotifyInteracted(nullptr));
	TestFalse(TEXT("flip: completion effects did not run"), Story->HasFact(DoneMarker));
	return true;
}

// 조우 문은 존재, 체력 준비 뒤에 사실 저장소를 묶고, GameState 가 바뀌면 새 저장소를 듣는다. 끝나면 놓는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterStorySourceTest, "VB.Unit.Encounter.StorySource",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterStorySourceTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UWorld* World = Fixture.World;
	UVBStoryStateComponent* First = VBStoryTestWorld::SpawnStory(*World);
	if (!TestNotNull(TEXT("first story"), First))
	{
		return false;
	}

	World->SetGameState(CastChecked<AGameStateBase>(First->GetOwner()));
	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(Pawn);
	Pawn->AddInstanceComponent(Gate);
	Gate->RegisterComponent();
	Pawn->DispatchBeginPlay();
	if (!TestTrue(TEXT("gate began play"), Gate->HasBegunPlay()))
	{
		return false;
	}

	TestTrue(TEXT("listens to the story present at BeginPlay"), First->OnStoryStateChanged.IsBoundToObject(Gate));

	UVBStoryStateComponent* Second = VBStoryTestWorld::SpawnStory(*World);
	if (!TestNotNull(TEXT("second story"), Second))
	{
		return false;
	}

	World->SetGameState(CastChecked<AGameStateBase>(Second->GetOwner()));
	TestTrue(TEXT("replaced GameState: listens to the new story"), Second->OnStoryStateChanged.IsBoundToObject(Gate));
	TestFalse(TEXT("replaced GameState: the old story is released"), First->OnStoryStateChanged.IsBoundToObject(Gate));

	// 객체는 살려 둔 채 끝낸다 - 파괴하면 약참조가 비어 '놓았다' 와 구별이 안 된다.
	Pawn->RouteEndPlay(EEndPlayReason::RemovedFromWorld);
	TestFalse(TEXT("EndPlay releases the current story"), Second->OnStoryStateChanged.IsBoundToObject(Gate));
	return true;
}

namespace VBEncounterDefeatTest
{
	// 다섯 적(A~E)의 '쓰러짐' 사실. 이야기 태그 대신 서로 부모, 자식이 아닌 네이티브 태그를 빌린다.
	TArray<FGameplayTag> EnemyFacts()
	{
		return {VBGameplayTags::Melee_Shape_Weapon, VBGameplayTags::Melee_Shape_HandL, VBGameplayTags::Melee_Shape_HandR,
			VBGameplayTags::Melee_Shape_FootL, VBGameplayTags::Melee_Shape_FootR};
	}

	TArray<FInstancedStruct>& GateArray(UVBEncounterGateComponent& Gate, const TCHAR* Name)
	{
		const FArrayProperty* Prop = FindFProperty<FArrayProperty>(UVBEncounterGateComponent::StaticClass(), Name);
		check(Prop);
		return *Prop->ContainerPtrToValuePtr<TArray<FInstancedStruct>>(&Gate);
	}

	// 배치한 적 하나: 제 사실이 꺼져 있을 때만 나오고, 쓰러지면 제 사실을 켠다(조우 문의 실제 BeginPlay, 쓰러짐 길).
	ACharacter* SpawnEnemy(UWorld& World, FGameplayTag Fact)
	{
		ACharacter* Body = World.SpawnActor<ACharacter>();
		UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(Body);
		FVBCondition_Fact NotDefeated;
		NotDefeated.Fact = Fact;
		NotDefeated.bNegate = true;
		GateArray(*Gate, TEXT("ExistConditions")).Add(FInstancedStruct::Make(NotDefeated));
		FVBEffect_SetFact Defeated;
		Defeated.Fact = Fact;
		GateArray(*Gate, TEXT("DefeatedEffects")).Add(FInstancedStruct::Make(Defeated));
		Body->AddInstanceComponent(Gate);
		Gate->RegisterComponent();
		Body->DispatchBeginPlay();
		return Body;
	}

	// 체력 없이 쓰러뜨리는 실제 길 - KillZ 아래에서 파괴되면 조우 문이 해소로 친다.
	void DefeatByFall(ACharacter& Body)
	{
		Body.SetActorLocation(FVector(0.0, 0.0, -10000.0));
		Body.Destroy();
	}

	bool Defeated(const UVBStoryStateComponent& Story, EVBRuleCompare Op, int32 Value)
	{
		FVBCondition_FactCount Count;
		Count.Facts = EnemyFacts();
		Count.Compare = Op;
		Count.Value = Value;
		FVBRuleContext Context;
		Context.Story = const_cast<UVBStoryStateComponent*>(&Story);
		return VBRules::EvaluateAll({FInstancedStruct::Make(Count)}, Context);
	}

	UVBStoryStateComponent* StartWorld(FAutomationTestBase& Test, FVBIsolatedGameWorld& Fixture)
	{
		if (!Fixture.Init(Test))
		{
			return nullptr;
		}

		AWorldSettings* Settings = Fixture.World->GetWorldSettings();
		Settings->bEnableWorldBoundsChecks = true;
		Settings->KillZ = -1000.0f;
		UVBStoryStateComponent* Story = VBStoryTestWorld::SpawnStory(*Fixture.World);
		if (Story)
		{
			Fixture.World->SetGameState(CastChecked<AGameStateBase>(Story->GetOwner()));
		}

		return Story;
	}
}

// 다섯 중 둘(A, D, 앞에서부터가 아님)을 쓰러뜨리고 저장 -> 새 월드에서 불러오기. 같은 둘만 안 나오고,
//   수는 저장된 합계가 아니라 사실에서 세어 2 그대로다. 남은 하나를 더 쓰러뜨리면 3 - 되살아난 적이 4 를 만들지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterDefeatPersistenceTest, "VB.Unit.Encounter.DefeatPersistence",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterDefeatPersistenceTest::RunTest(const FString& Parameters)
{
	using namespace VBEncounterDefeatTest;
	const TArray<FGameplayTag> Facts = EnemyFacts();
	TArray<uint8> Bytes;
	{
		FVBIsolatedGameWorld Fixture;
		UVBStoryStateComponent* Story = StartWorld(*this, Fixture);
		if (!TestNotNull(TEXT("first story"), Story))
		{
			return false;
		}

		TArray<ACharacter*> Enemies;
		for (const FGameplayTag& Fact : Facts)
		{
			Enemies.Add(SpawnEnemy(*Fixture.World, Fact));
		}

		for (ACharacter* Enemy : Enemies)
		{
			if (!TestTrue(TEXT("fresh start: every enemy exists"), IsValid(Enemy) && !Enemy->IsActorBeingDestroyed()))
			{
				return false;
			}
		}

		DefeatByFall(*Enemies[0]);
		DefeatByFall(*Enemies[3]);
		TestTrue(TEXT("A defeated fact"), Story->HasFact(Facts[0]));
		TestTrue(TEXT("D defeated fact"), Story->HasFact(Facts[3]));
		TestTrue(TEXT("count 2 before save"), Defeated(*Story, EVBRuleCompare::Equal, 2));

		UVBSaveGame* Save = NewObject<UVBSaveGame>();
		Story->WriteSave(*Save, FVBSaveContext());
		TestTrue(TEXT("save serializes"), UGameplayStatics::SaveGameToMemory(Save, Bytes));
	}

	FVBIsolatedGameWorld Fixture;
	UVBStoryStateComponent* Story = StartWorld(*this, Fixture);
	const UVBSaveGame* Loaded = Cast<UVBSaveGame>(UGameplayStatics::LoadGameFromMemory(Bytes));
	if (!TestNotNull(TEXT("second story"), Story) || !TestNotNull(TEXT("save deserializes"), Loaded))
	{
		return false;
	}
	// 불러오기는 월드 BeginPlay 전에 사실을 적용한다(AVBGameMode::HandleStartingNewPlayer) - 적을 놓기 전에 읽는다.
	Story->ReadSave(*Loaded, FVBSaveContext());
	TArray<ACharacter*> Enemies;
	for (const FGameplayTag& Fact : Facts)
	{
		Enemies.Add(SpawnEnemy(*Fixture.World, Fact));
	}

	const auto Exists = [](const ACharacter* Enemy) { return IsValid(Enemy) && !Enemy->IsActorBeingDestroyed(); };
	TestFalse(TEXT("reload: A stays defeated"), Exists(Enemies[0]));
	TestTrue(TEXT("reload: B returns"), Exists(Enemies[1]));
	TestTrue(TEXT("reload: C returns"), Exists(Enemies[2]));
	TestFalse(TEXT("reload: D stays defeated"), Exists(Enemies[3]));
	TestTrue(TEXT("reload: E returns"), Exists(Enemies[4]));
	TestTrue(TEXT("reload: count is still 2"), Defeated(*Story, EVBRuleCompare::Equal, 2));
	TestTrue(TEXT("reload: any-two wake holds"), Defeated(*Story, EVBRuleCompare::AtLeast, 2));
	TestFalse(TEXT("reload: any-four wake does not"), Defeated(*Story, EVBRuleCompare::AtLeast, 4));
	if (Exists(Enemies[2]))
	{
		DefeatByFall(*Enemies[2]);
	}

	TestTrue(TEXT("one more defeat after reload: count 3"), Defeated(*Story, EVBRuleCompare::Equal, 3));
	TestFalse(TEXT("encounter not complete"), Defeated(*Story, EVBRuleCompare::Equal, 5));
	return true;
}

#if WITH_EDITOR
namespace VBTransientSceneTest
{
	template <typename T>
	T& Field(UObject& Owner, const TCHAR* Name)
	{
		const FProperty* Prop = FindFProperty<FProperty>(Owner.GetClass(), Name);
		check(Prop);
		return *Prop->ContainerPtrToValuePtr<T>(&Owner);
	}

	// 재생 plugin 에 모듈 의존 없는 시네마틱 정의(규칙은 객체 identity 만 비교한다)
	UObject* NewScene()
	{
		UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/VBCinematicPlayback.VBCinematicDefinition"));
		return Class ? NewObject<UObject>(GetTransientPackage(), Class) : nullptr;
	}

	FInstancedStruct CompletedRead(UObject* Scene)
	{
		FInstancedStruct Item = FInstancedStruct::Make<FVBCondition_CinematicCompleted>();
		const FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(FVBCondition_CinematicCompleted::StaticStruct()->FindPropertyByName(TEXT("Definition")));
		check(Prop);
		Prop->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(Item.GetMutableMemory()), Scene);
		return Item;
	}

	FInstancedStruct PlayEffect(UObject* Scene)
	{
		FInstancedStruct Item = FInstancedStruct::Make<FVBEffect_PlayCinematic>();
		const FObjectPropertyBase* Prop = CastField<FObjectPropertyBase>(FVBEffect_PlayCinematic::StaticStruct()->FindPropertyByName(TEXT("Definition")));
		check(Prop);
		Prop->SetObjectPropertyValue(Prop->ContainerPtrToValuePtr<void>(Item.GetMutableMemory()), Scene);
		return Item;
	}
}

// 영역이 한 번만 트는 장면의 (저장 안 되는) 완료 영수증으로 깨는 관문은 저작 때 거절한다 - 다시 불러오면 장면이 안 나와
//   관문이 닫힌 채 남는다. 영역의 OnceFact 로 깨는 것은 유효하다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBEncounterTransientSceneWakeTest, "VB.Unit.Encounter.TransientSceneWake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBEncounterTransientSceneWakeTest::RunTest(const FString& Parameters)
{
	using namespace VBTransientSceneTest;
	FVBIsolatedGameWorld Fixture;
	if (!Fixture.Init(*this))
	{
		return false;
	}

	UObject* Scene = NewScene();
	if (!TestNotNull(TEXT("cinematic definition class"), Scene))
	{
		return false;
	}

	AVBAreaTrigger* Area = Fixture.World->SpawnActor<AVBAreaTrigger>();
	ACharacter* Body = Fixture.World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("area"), Area) || !TestNotNull(TEXT("body"), Body))
	{
		return false;
	}

	const FGameplayTag Once = VBGameplayTags::Melee_Shape_Weapon;
	Field<TArray<FInstancedStruct>>(*Area, TEXT("Effects")) = {PlayEffect(Scene)};
	Field<FGameplayTag>(*Area, TEXT("OnceFact")) = Once;
	UVBEncounterGateComponent* Gate = NewObject<UVBEncounterGateComponent>(Body);
	Body->AddInstanceComponent(Gate);
	Gate->RegisterComponent();
	TArray<FInstancedStruct>& Wake = Field<TArray<FInstancedStruct>>(*Gate, TEXT("WakeConditions"));

	Wake = {CompletedRead(Scene)};
	FDataValidationContext Transient;
	TestTrue(TEXT("waking on the receipt of a once-played scene is invalid"),
		Gate->IsDataValid(Transient) == EDataValidationResult::Invalid);
	TestEqual(TEXT("one error naming the mismatch"), static_cast<int32>(Transient.GetNumErrors()), 1);

	FVBCondition_Fact Seen;
	Seen.Fact = Once;
	Wake = {FInstancedStruct::Make(Seen)};
	FDataValidationContext Saved;
	TestTrue(TEXT("waking on the area's saved OnceFact is valid"), Gate->IsDataValid(Saved) != EDataValidationResult::Invalid);

	Wake = {CompletedRead(Scene)};
	Field<FGameplayTag>(*Area, TEXT("OnceFact")) = FGameplayTag();
	FDataValidationContext Replays;
	TestTrue(TEXT("a scene that replays after a reload may be waited on"), Gate->IsDataValid(Replays) != EDataValidationResult::Invalid);
	return true;
}
#endif

#endif
