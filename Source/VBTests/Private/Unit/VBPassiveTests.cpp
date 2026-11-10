// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystemGlobals.h"
#include "Engine/Engine.h"
#include "Engine/World.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBCombatRuleTable.h"
#include "Passive/VBPassiveComponent.h"
#include "Passive/VBPassiveSetDefinition.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "AbilitySystem/Abilities/VBGA_GuardShield.h"
#include "Passive/VBPassiveRules.h"
#include "VBPassiveTestListener.h"
#include "GameFramework/GameStateBase.h"
#include "Inventory/VBInventoryComponent.h"
#include "Inventory/VBItemDefinition.h"
#include "Narrative/Story/VBStoryStateComponent.h"

#if WITH_AUTOMATION_TESTS
namespace VBPassiveTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	FVBPassiveDefinition MakePassive(const FGameplayTag& Tag)
	{
		FVBPassiveDefinition Passive;
		Passive.PassiveTag = Tag;
		Passive.DisplayName = FText::FromString(Tag.ToString());
		return Passive;
	}

	void SetPassiveSet(UVBPassiveComponent* Passives, UVBPassiveSetDefinition* Set)
	{
		FObjectPropertyBase* Property = CastField<FObjectPropertyBase>(UVBPassiveComponent::StaticClass()->FindPropertyByName(TEXT("PassiveSet")));
		check(Property);
		Property->SetObjectPropertyValue_InContainer(Passives, Set);
	}

	FGameplayTagContainer RequiredFor(const UVBCombatRuleTable& Table, const FGameplayTag& Identity)
	{
		FGameplayTagContainer Blocked;
		FGameplayTagContainer Required;
		Table.AppendActivationTags(FGameplayTagContainer(Identity), Blocked, Required);
		return Required;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPassiveComponentTest, "VB.Unit.Passive.Component", VBPassiveTest::Flags)
bool FVBPassiveComponentTest::RunTest(const FString& Parameters)
{
	// 실제 AVBPlayerState: 주기 -> ASC 태그, 두 번 주기, 목록 밖 태그, 저장 쓰기, 읽기(저장 뒤에 얻은 것은 태그째 빠지고 "불러옴" 신호라 연출 없음).
	using namespace VBPassiveTest;
	const TStrongObjectPtr<UVBPassiveSetDefinition> Set(NewObject<UVBPassiveSetDefinition>());
	Set->Passives = {MakePassive(VBGameplayTags::Passive_Dodge), MakePassive(VBGameplayTags::Passive_Parry)};

	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false)
		.CreateAISystem(false).ShouldSimulatePhysics(false).SetTransactional(false);
	UWorld* World = GEngine ? UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num,
		&Init) : nullptr;
	if (!TestNotNull(TEXT("owned world"), World))
	{
		return false;
	}

	UPackage* Package = World->GetPackage();
	GEngine->CreateNewWorldContext(EWorldType::Game).SetCurrentWorld(World);
	World->InitializeActorsForPlay(FURL());
	AVBPlayerState* State = World->SpawnActor<AVBPlayerState>();
	UVBPassiveComponent* Passives = UVBPassiveComponent::FindFor(State);
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(State);
	if (TestNotNull(TEXT("passives on the player state"), Passives) && TestNotNull(TEXT("ASC"), ASC))
	{
		SetPassiveSet(Passives, Set.Get());
		TArray<bool> Signals;
		Passives->OnPassivesChanged.AddLambda([&Signals](bool bRestored) { Signals.Add(bRestored); });
		const TStrongObjectPtr<UVBPassiveTestListener> Listener(NewObject<UVBPassiveTestListener>());
		Passives->OnPassivesChangedBP.AddDynamic(Listener.Get(), &UVBPassiveTestListener::Heard);

		TestEqual(TEXT("no passive at start"), ASC->GetTagCount(VBGameplayTags::Passive_Dodge), 0);
		TestTrue(TEXT("dodge acquired"), Passives->Grant(VBGameplayTags::Passive_Dodge) == EVBPassiveGrantResult::Acquired);
		TestEqual(TEXT("dodge tag on the ASC"), ASC->GetTagCount(VBGameplayTags::Passive_Dodge), 1);
		TestTrue(TEXT("second grant is a no-op"), Passives->Grant(VBGameplayTags::Passive_Dodge) == EVBPassiveGrantResult::AlreadyAcquired);
		TestEqual(TEXT("still one dodge tag"), ASC->GetTagCount(VBGameplayTags::Passive_Dodge), 1);
		AddExpectedError(TEXT("에 없다 - 아이템의 얻기 효과가 틀렸다"), EAutomationExpectedErrorFlags::Contains, 4);
		TestTrue(TEXT("a passive outside the set is refused"),
			Passives->Grant(VBGameplayTags::Passive_Execute) == EVBPassiveGrantResult::UnknownPassive);
		TestEqual(TEXT("refused passive leaves no tag"), ASC->GetTagCount(VBGameplayTags::Passive_Execute), 0);
		TestTrue(TEXT("a live grant signals a reveal"), Signals.Num() == 1 && !Signals[0]);

		TestEqual(TEXT("Blueprint receives the same live signal without duplicate reveal"), Listener->RestoredSignals, Signals);

		bool bEffectFailed = false;
		FVBRuleContext EffectContext;
		EffectContext.Instigator = State;
		EffectContext.FailureSink = &bEffectFailed;
		FVBEffect_GrantPassive Effect;
		Effect.PassiveTag = VBGameplayTags::Passive_Execute;
		Effect.Apply(EffectContext);
		TestTrue(TEXT("unknown passive reports effect failure"), bEffectFailed);
		bEffectFailed = false;
		Effect.PassiveTag = VBGameplayTags::Passive_Dodge;
		Effect.Apply(EffectContext);
		TestFalse(TEXT("already acquired is successful"), bEffectFailed);
		TestEqual(TEXT("effect failure or duplicate adds no reveal"), Listener->RestoredSignals.Num(), 1);

		// 실제 획득 경로가 수량과 교환 비용을 바꾸기 전에 패시브 거절을 받는지 확인한다.
		AGameStateBase* GameState = World->SpawnActor<AGameStateBase>();
		UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(State);
		if (TestNotNull(TEXT("acquisition game state"), GameState) && TestNotNull(TEXT("owned inventory"), Inventory))
		{
			World->SetGameState(GameState);
			UVBStoryStateComponent* Story = NewObject<UVBStoryStateComponent>(GameState);
			GameState->AddInstanceComponent(Story);
			Story->RegisterComponent();
			UVBItemDefinition* Cost = NewObject<UVBItemDefinition>();
			Cost->ItemId = FGameplayTag::RequestGameplayTag(TEXT("Item.Medical.Gauze"));
			Cost->MaxCount = 10;
			TestEqual(TEXT("exchange fixture owns the cost"), Inventory->AddItem(Cost, 2), 2);
			UVBItemDefinition* Bound = NewObject<UVBItemDefinition>();
			Bound->ItemId = FGameplayTag::RequestGameplayTag(TEXT("Item.Key.DodgeToken"));
			Bound->MaxCount = 1;
			Bound->bCanDiscard = false;
			FVBItemFragment_AcquireEffects Acquire;
			FVBEffect_GrantPassive Unknown;
			Unknown.PassiveTag = VBGameplayTags::Passive_Execute;
			Acquire.Effects.Add(FInstancedStruct::Make(Unknown));
			Bound->Fragments.Add(FInstancedStruct::Make(Acquire));
			int32 InventorySignals = 0;
			const FDelegateHandle InventoryHandle = Inventory->OnInventoryChanged.AddLambda([&InventorySignals]() { ++InventorySignals; });
			AddExpectedMessagePlain(TEXT("effect preflight refused"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 2);
			TestEqual(TEXT("invalid bound acquisition is refused"), Inventory->AddItem(Bound, 1), 0);
			TestFalse(TEXT("invalid bound exchange is refused"), Inventory->TryExchange({{Cost, 1}}, {{Bound, 1}}));
			TestEqual(TEXT("refused acquisition leaves no item"), Inventory->GetCount(Bound), 0);
			TestEqual(TEXT("refused exchange keeps the cost"), Inventory->GetCount(Cost), 2);
			TestEqual(TEXT("preflight failure sends no inventory notification"), InventorySignals, 0);
			TestEqual(TEXT("preflight failure sends no passive reveal"), Listener->RestoredSignals.Num(), 1);
			Bound->Fragments[0].GetMutable<FVBItemFragment_AcquireEffects>().Effects[0].GetMutable<FVBEffect_GrantPassive>().PassiveTag = VBGameplayTags::Passive_Dodge;
			TestTrue(TEXT("fixed item can retry the same exchange"), Inventory->TryExchange({{Cost, 1}}, {{Bound, 1}}));
			TestEqual(TEXT("retry grants exactly one bound item"), Inventory->GetCount(Bound), 1);
			TestEqual(TEXT("successful retry consumes cost once"), Inventory->GetCount(Cost), 1);
			TestEqual(TEXT("successful retry notifies once"), InventorySignals, 1);
			TestEqual(TEXT("already owned passive does not reveal again"), Listener->RestoredSignals.Num(), 1);
			Inventory->OnInventoryChanged.Remove(InventoryHandle);
		}

		FVBSaveContext Context;
		Context.World = World;
		Context.PlayerState = State;
		UVBSaveGame* Save = NewObject<UVBSaveGame>();
		Passives->WriteSave(*Save, Context);
		TestEqual(TEXT("save keeps dodge"), Save->Passives.Acquired, TArray<FGameplayTag>{VBGameplayTags::Passive_Dodge});

		Passives->Grant(VBGameplayTags::Passive_Parry);
		Save->Passives.Acquired.Add(VBGameplayTags::Passive_Execute);
		Signals.Reset();
		Listener->RestoredSignals.Reset();
		Passives->ReadSave(*Save, Context);
		Passives->ReadSave(*Save, Context);
		TestEqual(TEXT("load restores only the saved passive"), Passives->GetAcquired(), TArray<FGameplayTag>{VBGameplayTags::Passive_Dodge});
		TestEqual(TEXT("load keeps one dodge tag"), ASC->GetTagCount(VBGameplayTags::Passive_Dodge), 1);
		TestEqual(TEXT("load removes the later parry tag"), ASC->GetTagCount(VBGameplayTags::Passive_Parry), 0);
		TestEqual(TEXT("load never adds a passive outside the set"), ASC->GetTagCount(VBGameplayTags::Passive_Execute), 0);
		TestTrue(TEXT("loads signal «restored» only (no reveal)"), Signals.Num() == 2 && Signals[0] && Signals[1]);

		// 목록이 빠진 채 패시브 없는 저장을 읽으면 조용하다(잃을 것이 없다) - 기대 오류를 걸지 않은 채 읽는다.
		TestEqual(TEXT("Blueprint restores silently with the native signal"), Listener->RestoredSignals, Signals);
		SetPassiveSet(Passives, nullptr);
		bEffectFailed = false;
		AddExpectedError(TEXT("를 주지 못했다 - PassiveSet 이 비었다"), EAutomationExpectedErrorFlags::Contains, 1);
		Effect.Apply(EffectContext);
		TestTrue(TEXT("missing set reports effect failure"), bEffectFailed);
		UVBSaveGame* EmptySave = NewObject<UVBSaveGame>();
		Passives->ReadSave(*EmptySave, Context);
		TestEqual(TEXT("no set + empty save: nothing acquired"), Passives->GetAcquired().Num(), 0);

		// 목록이 빠진 채 패시브가 든 저장을 읽으면 오류를 내고 저장을 지우지 않는다 - 그대로 쥐고 다시 쓰면 같은 목록.
		Save->Passives.Acquired = {VBGameplayTags::Passive_Dodge, VBGameplayTags::Passive_Riposte};
		AddExpectedError(TEXT("패시브 로드: PassiveSet 이 비었다"), EAutomationExpectedErrorFlags::Contains, 1);
		Passives->ReadSave(*Save, Context);
		UVBSaveGame* Rewritten = NewObject<UVBSaveGame>();
		Passives->WriteSave(*Rewritten, Context);
		TestEqual(TEXT("no set: the saved list survives a rewrite"), Rewritten->Passives.Acquired, Save->Passives.Acquired);
		TestEqual(TEXT("no set: the saved passives still get their tags"), ASC->GetTagCount(VBGameplayTags::Passive_Riposte), 1);
		Passives->OnPassivesChangedBP.RemoveDynamic(Listener.Get(), &UVBPassiveTestListener::Heard);
		Passives->OnPassivesChanged.Clear();
	}

	World->DestroyWorld(false);
	GEngine->DestroyWorldContext(World);
	Package->SetDirtyFlag(false);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPassiveSetValidationTest, "VB.Unit.Passive.SetValidation", VBPassiveTest::Flags)
bool FVBPassiveSetValidationTest::RunTest(const FString& Parameters)
{
	using namespace VBPassiveTest;
	const TStrongObjectPtr<UVBPassiveSetDefinition> Set(NewObject<UVBPassiveSetDefinition>());
	Set->Passives = {MakePassive(VBGameplayTags::Passive_Dodge), MakePassive(VBGameplayTags::Passive_Riposte)};
	TestEqual(TEXT("a valid set has no problems"), Set->FindProblems().Num(), 0);
	TestNotNull(TEXT("find by tag"), Set->FindPassive(VBGameplayTags::Passive_Riposte));
	TestNull(TEXT("an empty tag finds nothing"), Set->FindPassive(FGameplayTag()));

	Set->Passives.Add(MakePassive(VBGameplayTags::Passive_Dodge));
	TestEqual(TEXT("a duplicate is one problem"), Set->FindProblems().Num(), 1);
	Set->Passives.Pop();
	Set->Passives.Add(MakePassive(VBGameplayTags::Passive));
	TestEqual(TEXT("the Passive root is not a passive"), Set->FindProblems().Num(), 1);
	Set->Passives.Pop();
	Set->Passives[0].DisplayName = FText::GetEmpty();
	TestEqual(TEXT("a nameless passive is one problem"), Set->FindProblems().Num(), 1);
	Set->Passives[0].DisplayName = FText::FromString(TEXT("Dodge"));
	Set->RevealTiming.HintStart = Set->RevealTiming.End + 1.0f;
	TestEqual(TEXT("reveal timing out of order is one problem"), Set->FindProblems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPassiveGateRowsTest, "VB.Unit.Passive.GateRows", VBPassiveTest::Flags)
bool FVBPassiveGateRowsTest::RunTest(const FString& Parameters)
{
	// 방패 가드 줄은 자식 태그(Guard.Shield)에만 걸린다 - Fighter 가드(부모 태그)는 패시브 없이 시작한다.
	using namespace VBPassiveTest;
	const TStrongObjectPtr<UVBCombatRuleTable> Table(NewObject<UVBCombatRuleTable>());
	FVBAbilityRule& Shield = Table->AbilityRules.AddDefaulted_GetRef();
	Shield.Ability = VBGameplayTags::Ability_Defense_Guard_Shield;
	Shield.RequiresState.AddTag(VBGameplayTags::Passive_Parry);
	FVBAbilityRule& Dodge = Table->AbilityRules.AddDefaulted_GetRef();
	Dodge.Ability = VBGameplayTags::Ability_Defense_Dodge;
	Dodge.RequiresState.AddTag(VBGameplayTags::Passive_Dodge);

	const FGameplayTagContainer& ShieldIdentity = GetDefault<UVBGA_GuardShield>()->GetAssetTags();
	TestTrue(TEXT("native shield carries the guard parent"), ShieldIdentity.HasTagExact(VBGameplayTags::Ability_Defense_Guard));
	TestTrue(TEXT("native shield carries the shield identity"), ShieldIdentity.HasTagExact(VBGameplayTags::Ability_Defense_Guard_Shield));
	TestFalse(TEXT("ordinary guard is not a shield"),
		GetDefault<UVBGA_Guard>()->GetAssetTags().HasTagExact(VBGameplayTags::Ability_Defense_Guard_Shield));
	FGameplayTagContainer Blocked;
	FGameplayTagContainer ShieldRequired;
	Table->AppendActivationTags(ShieldIdentity, Blocked, ShieldRequired);
	TestTrue(TEXT("shield guard requires Passive.Parry"), ShieldRequired.HasTagExact(VBGameplayTags::Passive_Parry));
	TestTrue(TEXT("Fighter guard requires nothing"), RequiredFor(*Table, VBGameplayTags::Ability_Defense_Guard).IsEmpty());
	TestTrue(TEXT("dodge requires Passive.Dodge"),
		RequiredFor(*Table, VBGameplayTags::Ability_Defense_Dodge).HasTagExact(VBGameplayTags::Passive_Dodge));
	return true;
}
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBPassiveEffectFailureTest, "VB.Unit.Passive.EffectFailure", VBPassiveTest::Flags)
bool FVBPassiveEffectFailureTest::RunTest(const FString& Parameters)
{
	bool bFailed = false;
	FVBRuleContext Context;
	Context.FailureSink = &bFailed;
	FVBEffect_GrantPassive Effect;
	Effect.PassiveTag = VBGameplayTags::Passive_Dodge;
	AddExpectedError(TEXT("패시브 컴포넌트가 없다"), EAutomationExpectedErrorFlags::Contains, 2);
	TestFalse(TEXT("missing passive owner refuses preflight"), Effect.CanApply(Context));
	Effect.Apply(Context);
	TestTrue(TEXT("missing passive owner reports effect failure"), bFailed);
	return true;
}
#endif
