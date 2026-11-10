#include "VBCinematicSkillGaugeTestTypes.h"
#include "EngineUtils.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayEffectContext.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "CharacterCommon/VBCinematicSkillChargeComponent.h"
#include "Data/VBCinematicSkillChargeRules.h"
#include "Data/VBCinematicSkillDefinition.h"
#include "Data/VBParryConfig.h"
#include "Player/VBPlayerState.h"
#include "SaveSystem/VBSaveGame.h"
#include "SaveSystem/VBSaveParticipantSubsystem.h"

AVBGaugeTestPlayerState::AVBGaugeTestPlayerState()
{
	ASC = CreateDefaultSubobject<UVBAbilitySystemComponent>(TEXT("ASC"));
	Health = CreateDefaultSubobject<UVBHealthAttributeSet>(TEXT("Health"));
	Combat = CreateDefaultSubobject<UVBCombatAttributeSet>(TEXT("Combat"));
}

AVBGaugeTestPawn::AVBGaugeTestPawn()
{
	Weapons = CreateDefaultSubobject<UVBGaugeTestWeapons>(TEXT("Weapons"));
	ParryConfig = CreateDefaultSubobject<UVBParryConfig>(TEXT("ParryConfig"));
	ParryConfig->ParryPoiseDamage = 0.0f;
}

const UVBParryConfig* AVBGaugeTestPawn::GetParryConfig() const { return ParryConfig; }

UVBGaugeTestGain::UVBGaugeTestGain()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat Amount;
	Amount.DataTag = VBGameplayTags::Data_CinematicSkillGaugeGain;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(Amount);
}

UVBGaugeTestCost::UVBGaugeTestCost()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute();
	Modifier.ModifierOp = EGameplayModOp::Override;
	Modifier.ModifierMagnitude = FScalableFloat(0.0f);
}

UVBGaugeTestDamage::UVBGaugeTestDamage()
{
	DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Modifier = Modifiers.AddDefaulted_GetRef();
	Modifier.Attribute = UVBHealthAttributeSet::GetIncomingDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	Modifier.ModifierMagnitude = FScalableFloat(10.0f);
}

UVBGaugeTestReadyFeedback::UVBGaugeTestReadyFeedback() { DurationPolicy = EGameplayEffectDurationType::Infinite; }

UVBGaugeTestSkill::UVBGaugeTestSkill()
{
	CostGameplayEffectClass = UVBGaugeTestCost::StaticClass();
	Definition = CreateDefaultSubobject<UVBCinematicSkillDefinition>(TEXT("Definition"));
}

#if WITH_DEV_AUTOMATION_TESTS
#include "Engine/GameInstance.h"
#include "Messages/VBGameplayMessageSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Serialization/MemoryReader.h"
#include "Serialization/MemoryWriter.h"
#include "VBIsolatedGameWorld.h"

namespace VBGaugeTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;
	FGameplayAttribute Current() { return UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute(); }
	FGameplayAttribute Maximum() { return UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute(); }

	struct FFixture
	{
		FVBIsolatedGameWorld Isolated;
		AVBGaugeTestPlayerState* Player = nullptr;
		AVBGaugeTestPawn* Pawn = nullptr;
		UVBCinematicSkillChargeComponent* Charge = nullptr;
		// 월드가 사라지기 전에 EndPlay 를 돌려 살아 있는 컴포넌트가 다음 시험의 GC 로 새지 않게 한다. 끄는 것은 그 누수 재현용이다
		bool bRouteEndPlayOnExit = true;

		~FFixture()
		{
			if (!bRouteEndPlayOnExit || !Isolated.World)
			{
				return;
			}

			for (TActorIterator<AActor> It(Isolated.World); It; ++It)
			{
				if (It->HasActorBegunPlay())
				{
					It->RouteEndPlay(EEndPlayReason::Quit);
				}
			}
		}

		bool Init(FAutomationTestBase& Test, bool bCharge = true)
		{
			if (!Isolated.Init(Test))
			{
				return false;
			}

			if (bCharge)
			{
				UGameInstance* Instance = nullptr;
				for (const FWorldContext& Context : GEngine->GetWorldContexts())
				{
					if ((Context.WorldType == EWorldType::Game || Context.WorldType == EWorldType::PIE)
						&& Context.World() && Context.World() != Isolated.World && Context.World()->GetGameInstance())
					{ Instance = Context.World()->GetGameInstance(); break; }
				}

				if (!Test.TestNotNull(TEXT("initialized game instance (-game automation)"), Instance))
				{
					return false;
				}

				Isolated.World->SetGameInstance(Instance);
				if (!Test.TestNotNull(TEXT("actual gameplay message bus"), UVBGameplayMessageSubsystem::Get(Isolated.World)))
				{
					return false;
				}
			}

			Player = Isolated.World->SpawnActor<AVBGaugeTestPlayerState>();
			Pawn = Isolated.World->SpawnActor<AVBGaugeTestPawn>();
			if (!Test.TestNotNull(TEXT("fixture player"), Player) || !Test.TestNotNull(TEXT("fixture pawn"), Pawn))
			{
				return false;
			}

			Pawn->ASC = Player->ASC;
			Player->ASC->InitAbilityActorInfo(Player, Pawn);
			if (!Test.TestNotNull(TEXT("registered combat attributes"), Player->ASC->GetSet<UVBCombatAttributeSet>()))
			{
				return false;
			}

			ResetHealth(*Player->ASC);
			Player->ASC->SetNumericAttributeBase(Maximum(), 100.0f);
			Pawn->Weapons->SetFixtureOwned(true);
			if (bCharge)
			{
				Charge = NewObject<UVBCinematicSkillChargeComponent>(Player);
				UVBCinematicSkillChargeRules* Rules = NewObject<UVBCinematicSkillChargeRules>(Charge);
				Rules->SkillDefinition = GetDefault<UVBGaugeTestSkill>()->GetDefinition();
				Rules->GainEffect = UVBGaugeTestGain::StaticClass();
				Rules->ReadyFeedbackEffect = UVBGaugeTestReadyFeedback::StaticClass();
				Charge->Rules = Rules;
				Player->AddInstanceComponent(Charge);
				Charge->RegisterComponent();
				Player->DispatchBeginPlay();
				Player->OnPawnSet.Broadcast(Player, Pawn, nullptr);
				return Test.TestTrue(TEXT("Fighter entitlement established"), Charge->IsUnlocked());
			}

			return true;
		}

		static void ResetHealth(UAbilitySystemComponent& ASC)
		{
			ASC.SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxHealthAttribute(), 100.0f);
			ASC.SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 100.0f);
		}

		FVBCombatChargeCredit MakeCredit(EVBCombatChargeReason Reason = EVBCombatChargeReason::Hit) const
		{
			FVBCombatChargeCredit Credit;
			Credit.Id = FGuid::NewGuid();
			Credit.RecipientASC = Player->ASC;
			Credit.RecipientAvatar = Pawn;
			Credit.Reason = Reason;
			Credit.AppliedDamage = 10.0f;
			return Credit;
		}

		float Value() const { return Player->ASC->GetNumericAttribute(Current()); }
		void Publish(const FVBCombatChargeCredit& Credit) const { VBCombatCharge::Publish(Isolated.World, Credit); }

		void Hit(FFixture& Victim, EVBCombatChargeSource Source, bool bFinisher = false)
		{
			FGameplayEffectContextHandle Context(new FVBGameplayEffectContext());
			Context.AddInstigator(Player, Pawn);
			FVBGameplayEffectContext::Get(Context)->SetChargeSource(Source);
			FGameplayEffectSpecHandle Spec = Player->ASC->MakeOutgoingSpec(UVBGaugeTestDamage::StaticClass(), 1.0f, Context);
			if (bFinisher)
			{
				Spec.Data->AddDynamicAssetTag(VBGameplayTags::Effect_Execute);
			}

			Player->ASC->ApplyGameplayEffectSpecToTarget(*Spec.Data.Get(), Victim.Player->ASC);
		}
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeAttributesTest, "VB.Unit.CinematicSkillGauge.Attributes", VBGaugeTest::Flags)
bool FVBGaugeAttributesTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this, false))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Player->ASC;
	ASC.SetNumericAttributeBase(Current(), 150.0f);
	TestEqual(TEXT("base and current capped"), ASC.GetNumericAttributeBase(Current()), 100.0f);
	TestEqual(TEXT("current capped"), F.Value(), 100.0f);
	ASC.SetNumericAttributeBase(Maximum(), 60.0f);
	TestEqual(TEXT("max drop clamps base"), ASC.GetNumericAttributeBase(Current()), 60.0f);
	TestEqual(TEXT("max drop clamps current"), F.Value(), 60.0f);
	ASC.SetNumericAttributeBase(Maximum(), 120.0f);
	TestEqual(TEXT("max increase never refills"), F.Value(), 60.0f);
	ASC.SetNumericAttributeBase(Current(), -10.0f);
	TestEqual(TEXT("negative clamped"), F.Value(), 0.0f);
	TestFalse(TEXT("0/0 is not full"), UVBCinematicSkillChargeRules::IsFull(0, 0));
	TestFalse(TEXT("below max is not full"), UVBCinematicSkillChargeRules::IsFull(119, 120));
	TestTrue(TEXT("full uses authored max"), UVBCinematicSkillChargeRules::IsFull(120, 120));
	const AVBPlayerState* Defaults = GetDefault<AVBPlayerState>();
	TestTrue(TEXT("current is saved"), Defaults->GetSavedAttributes().Contains(Current()));
	TestFalse(TEXT("max belongs to init GE"), Defaults->GetSavedAttributes().Contains(Maximum()));
	TestEqual(TEXT("restore cap"), Defaults->GetSavedAttributeCaps().FindRef(Current()), Maximum());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeCreditsTest, "VB.Unit.CinematicSkillGauge.Credits", VBGaugeTest::Flags)
bool FVBGaugeCreditsTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	FVBCombatChargeCredit Credit = F.MakeCredit();
	F.Publish(Credit);
	TestEqual(TEXT("ordinary resolved hit"), F.Value(), 2.0f);
	F.Publish(Credit);
	TestEqual(TEXT("duplicate event cannot pay twice"), F.Value(), 2.0f);
	F.Publish(F.MakeCredit(EVBCombatChargeReason::Parry));
	TestEqual(TEXT("parry bonus"), F.Value(), 17.0f);
	F.Player->ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_Finishing);
	F.Publish(F.MakeCredit(EVBCombatChargeReason::Finisher));
	TestEqual(TEXT("final finisher earns while body is busy"), F.Value(), 47.0f);
	F.Player->ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Finishing);
	F.Player->ASC->AddLooseGameplayTag(VBGameplayTags::State_Combat_CinematicSkill);
	F.Publish(F.MakeCredit());
	TestEqual(TEXT("own skill cannot charge"), F.Value(), 47.0f);
	F.Player->ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Combat_CinematicSkill);
	F.Player->ASC->AddLooseGameplayTag(VBGameplayTags::State_Dead);
	F.Publish(F.MakeCredit());
	TestEqual(TEXT("dead owner does not charge"), F.Value(), 47.0f);
	F.Player->ASC->RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	F.Player->OnPawnSet.Broadcast(F.Player, nullptr, F.Pawn);
	F.Publish(F.MakeCredit());
	TestEqual(TEXT("credit between bodies is ignored (ASC still names the old body)"), F.Value(), 47.0f);
	F.Player->OnPawnSet.Broadcast(F.Player, F.Pawn, nullptr);
	Credit = F.MakeCredit(); Credit.AppliedDamage = 0; F.Publish(Credit);
	Credit = F.MakeCredit(); Credit.RecipientAvatar = nullptr; F.Publish(Credit);
	TestEqual(TEXT("invalid outcome or stale avatar does not charge"), F.Value(), 47.0f);
	F.Pawn->Weapons->SetFixtureOwned(false);
	Credit = F.MakeCredit(); F.Publish(Credit);
	F.Pawn->Weapons->SetFixtureOwned(true);
	F.Publish(Credit);
	TestEqual(TEXT("locked credit cannot be banked for later entitlement"), F.Value(), 47.0f);
	UVBSaveParticipantSubsystem::Register(F.Player);
	Credit = F.MakeCredit();
	F.Player->DuringSaveRead = [&]() { F.Publish(Credit); };
	FVBSaveContext Context;
	Context.World = F.Isolated.World;
	Context.PlayerState = F.Player;
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	F.Isolated.World->GetSubsystem<UVBSaveParticipantSubsystem>()->ReadAll(*Save, Context);
	F.Player->DuringSaveRead = nullptr;
	UVBSaveParticipantSubsystem::Unregister(F.Player);
	F.Publish(Credit);
	TestEqual(TEXT("save-application credit is discarded, not deferred"), F.Value(), 47.0f);
	F.Player->ASC->SetNumericAttributeBase(Current(), 99.0f);
	F.Publish(F.MakeCredit(EVBCombatChargeReason::Finisher));
	TestEqual(TEXT("overflow discarded"), F.Value(), 100.0f);
	F.Player->ASC->SetNumericAttributeBase(Current(), 0.0f);
	Credit = F.MakeCredit();
	bool bReentered = false;
	const FDelegateHandle Nested = F.Player->ASC->GetGameplayAttributeValueChangeDelegate(Current()).AddLambda(
		[&](const FOnAttributeChangeData&) { if (!bReentered) { bReentered = true; F.Publish(Credit); } });
	F.Publish(Credit);
	F.Player->ASC->GetGameplayAttributeValueChangeDelegate(Current()).Remove(Nested);
	TestTrue(TEXT("attribute callback actually reentered"), bReentered);
	TestEqual(TEXT("receipt precedes nested attribute callbacks"), F.Value(), 2.0f);
	F.Charge->DestroyComponent();
	F.Player->ASC->SetNumericAttributeBase(Current(), 0.0f);
	F.Publish(F.MakeCredit());
	TestEqual(TEXT("teardown removes bus listener"), F.Value(), 0.0f);
	return true;
}

// 크래시 모양 - 시험 월드가 EndPlay 없이 파괴된 뒤, 나중 GC 가 월드 subsystem 이 이미 없는 상태에서 BeginDestroy 로 컴포넌트를 끝낸다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeTeardownTest, "VB.Unit.CinematicSkillGauge.TeardownAfterWorld", VBGaugeTest::Flags)
bool FVBGaugeTeardownTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	TWeakObjectPtr<UVBCinematicSkillChargeComponent> Weak;
	{
		FFixture F;
		F.bRouteEndPlayOnExit = false;
		if (!F.Init(*this))
		{
			return false;
		}

		Weak = F.Charge;
		if (!TestTrue(TEXT("component began play"), F.Charge->HasBegunPlay()))
		{
			return false;
		}
	}

	CollectGarbage(GARBAGE_COLLECTION_KEEPFLAGS);
	TestFalse(TEXT("GC after the world is gone ended the component (no world lookup in EndPlay)"), Weak.IsValid());
	return true;
}

// 일부러 따로 둔 시험이다 - UE 는 다른 오류가 없을 때만 기대 로그를 검사하므로(!HasAnyErrors() && HasMetExpectedMessages())
// 큰 시험 안에선 다른 실패가 빠진 메시지를 가린다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeNoMaximumTest, "VB.Unit.CinematicSkillGauge.NoMaximum", VBGaugeTest::Flags)
bool FVBGaugeNoMaximumTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	F.Player->ASC->SetNumericAttributeBase(Maximum(), 0.0f);
	AddExpectedMessagePlain(TEXT("MaxCinematicSkillGauge is 0"), ELogVerbosity::Error, EAutomationExpectedMessageFlags::Contains, 1);
	F.Publish(F.MakeCredit());
	F.Publish(F.MakeCredit());
	F.Player->ASC->SetNumericAttributeBase(Maximum(), 100.0f);
	TestEqual(TEXT("no maximum: the credits are refused, not banked"), F.Value(), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeReadyTest, "VB.Unit.CinematicSkillGauge.ReadyLifecycle", VBGaugeTest::Flags)
bool FVBGaugeReadyTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Player->ASC;
	// 빈 게이지로 해금하면 ready 는 그대로지만, HUD 는 게이지를 보이려고 알림을 받아야 한다
	UVBGaugeTestStateListener* Listener = NewObject<UVBGaugeTestStateListener>();
	F.Charge->OnGaugeStateChanged.AddDynamic(Listener, &UVBGaugeTestStateListener::Heard);
	F.Pawn->Weapons->SetFixtureOwned(false);
	Listener->Count = 0;
	F.Pawn->Weapons->SetFixtureOwned(true);
	TestTrue(TEXT("unlock with an empty gauge is announced"), Listener->Count > 0 && F.Charge->IsUnlocked());
	F.Charge->OnGaugeStateChanged.RemoveDynamic(Listener, &UVBGaugeTestStateListener::Heard);
	F.Pawn->Weapons->SetFixtureSelected(EVBWeaponType::Fighter);
	ASC.SetNumericAttributeBase(Current(), 100.0f);
	TestFalse(TEXT("full and owned is insufficient without granted skill"), F.Charge->IsReady());
	FGameplayAbilitySpecHandle Skill = ASC.GiveAbility(FGameplayAbilitySpec(UVBGaugeTestSkill::StaticClass()));
	// 주인은 하나다 - 아래 모든 상태에서 ready 는 스킬 자신의 입장 판정과 같아야 한다
	const auto SkillWouldGo = [&]()
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Skill);
		return Spec && Spec->Ability && Spec->Ability->CanActivateAbility(Skill, ASC.AbilityActorInfo.Get());
	};
	const auto ExpectAgreement = [&](const TCHAR* State)
	{
		TestEqual(FString::Printf(TEXT("ready agrees with the skill's admission: %s"), State), F.Charge->IsReady(), SkillWouldGo());
	};
	TestTrue(TEXT("actual skill permission enables ready"), F.Charge->IsReady());
	ExpectAgreement(TEXT("full"));
	TestEqual(TEXT("one ready receipt"), ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 1);
	ASC.ClearAbility(Skill);
	TestFalse(TEXT("direct ability removal releases ready before another combat event"), F.Charge->IsReady());
	TestEqual(TEXT("removed ability cannot retain ready effect"),
		ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 0);
	Skill = ASC.GiveAbility(FGameplayAbilitySpec(UVBGaugeTestSkill::StaticClass()));
	TestTrue(TEXT("grant restores readiness"), F.Charge->IsReady());
	F.Publish(F.MakeCredit());
	TestEqual(TEXT("repeated capped gain never stacks ready"),
		ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 1);
	F.Pawn->Weapons->SetFixtureSelected(EVBWeaponType::Blade);
	TestFalse(TEXT("other stance turns off ready"), F.Charge->IsReady());
	ExpectAgreement(TEXT("other stance"));
	TestEqual(TEXT("other stance removes only the ready receipt"),
		ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 0);
	F.Pawn->Weapons->SetFixtureSelected(EVBWeaponType::Fighter);
	TestTrue(TEXT("returning stance restores ready"), F.Charge->IsReady());
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Dead);
	TestFalse(TEXT("death turns off ready"), F.Charge->IsReady());
	ExpectAgreement(TEXT("dead"));
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	TestTrue(TEXT("cleared death reconciles permission"), F.Charge->IsReady());
	ASC.SetNumericAttributeBase(Current(), 99.0f);
	TestFalse(TEXT("not full turns off ready"), F.Charge->IsReady());
	ExpectAgreement(TEXT("not full"));
	ASC.SetNumericAttributeBase(Current(), 100.0f);
	// 저장 적용은 HUD 만이 아니라 스킬 자체의 관문이다
	UVBSaveParticipantSubsystem::Register(F.Player);
	bool bRefusedDuringSave = false;
	F.Player->DuringSaveRead = [&]() { bRefusedDuringSave = !SkillWouldGo(); };
	FVBSaveContext SaveContext;
	SaveContext.World = F.Isolated.World;
	SaveContext.PlayerState = F.Player;
	UVBSaveGame* SaveGame = NewObject<UVBSaveGame>();
	F.Isolated.World->GetSubsystem<UVBSaveParticipantSubsystem>()->ReadAll(*SaveGame, SaveContext);
	F.Player->DuringSaveRead = nullptr;
	UVBSaveParticipantSubsystem::Unregister(F.Player);
	TestTrue(TEXT("the skill refuses while a save applies"), bRefusedDuringSave);
	TestTrue(TEXT("finalized save reconciles ready"), F.Charge->IsReady());
	AVBGaugeTestPawn* Replacement = F.Isolated.World->SpawnActor<AVBGaugeTestPawn>();
	if (!TestNotNull(TEXT("replacement body"), Replacement))
	{
		return false;
	}

	Replacement->ASC = &ASC;
	Replacement->Weapons->SetFixtureOwned(true);
	Replacement->Weapons->SetFixtureSelected(EVBWeaponType::Fighter);
	ASC.InitAbilityActorInfo(F.Player, Replacement);
	F.Player->OnPawnSet.Broadcast(F.Player, Replacement, F.Pawn);
	TestTrue(TEXT("replacement body reconciled"), F.Charge->IsReady());
	TestFalse(TEXT("old body's ownership source detached"), F.Pawn->Weapons->OnOwnedWeaponsChanged.IsBoundToObject(F.Charge));
	TestEqual(TEXT("replacement owns exactly one ready receipt"),
		ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 1);
	F.Charge->DestroyComponent();
	TestEqual(TEXT("end removes ready effect"), ASC.GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 0);
	TestFalse(TEXT("end removes attribute callback"), ASC.GetGameplayAttributeValueChangeDelegate(Current()).IsBoundToObject(F.Charge));
	ASC.ClearAbility(Skill);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeLateAbilityTest, "VB.Unit.CinematicSkillGauge.LateAbilityNotification", VBGaugeTest::Flags)
bool FVBGaugeLateAbilityTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this))
	{
		return false;
	}

	UVBAbilitySystemComponent* ASC = CastChecked<UVBAbilitySystemComponent>(F.Player->ASC);
	F.Pawn->Weapons->SetFixtureSelected(EVBWeaponType::Fighter);
	ASC->SetNumericAttributeBase(Current(), 100.0f);
	// 서버 전용 spec-dirtied 알림 없이 목록만 받는 클라이언트를 흉내 낸다
	ASC->AbilitySpecDirtiedCallbacks.RemoveAll(F.Charge);
	const FGameplayAbilitySpecHandle Skill = ASC->GiveAbility(FGameplayAbilitySpec(UVBGaugeTestSkill::StaticClass()));
	TestFalse(TEXT("a late list must wait for its notification"), F.Charge->IsReady());
	ASC->OnRep_ActivateAbilities();
	TestTrue(TEXT("real OnRep reconciliation wakes the ready consumer"), F.Charge->IsReady());
	TestEqual(TEXT("one ready effect after list readback"),
		ASC->GetGameplayEffectCount(UVBGaugeTestReadyFeedback::StaticClass(), nullptr), 1);
	ASC->ClearAbility(Skill);
	TestFalse(TEXT("removed ability releases ready"), F.Charge->IsReady());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeResolutionTest, "VB.Unit.CinematicSkillGauge.ResolvedDamage", VBGaugeTest::Flags)
bool FVBGaugeResolutionTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture Attacker, Defender;
	if (!Attacker.Init(*this) || !Defender.Init(*this))
	{
		return false;
	}

	Defender.Pawn->Team = 1;
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("actual health write charges attacker"), Attacker.Value(), 2.0f);
	TestEqual(TEXT("victim takes no damage-received credit"), Defender.Value(), 0.0f);
	Defender.Pawn->bGuarding = true;
	Defender.Pawn->GuardElapsed = 0.0f;
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("actual perfect parry charges defender"), Defender.Value(), 15.0f);
	TestEqual(TEXT("parried attacker does not charge"), Attacker.Value(), 2.0f);
	Defender.Pawn->GuardElapsed = 10.0f;
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("block grants no defender credit"), Defender.Value(), 15.0f);
	TestEqual(TEXT("blocked chip grants no attacker credit"), Attacker.Value(), 2.0f);
	Defender.Pawn->bGuarding = false;
	Attacker.Hit(Defender, EVBCombatChargeSource::CinematicSkill);
	TestEqual(TEXT("CS0 source remains excluded after its active tag ended"), Attacker.Value(), 2.0f);
	Attacker.Hit(Defender, EVBCombatChargeSource::Unspecified);
	TestEqual(TEXT("unclassified damage cannot charge"), Attacker.Value(), 2.0f);
	Attacker.Hit(Defender, EVBCombatChargeSource::Unspecified, true);
	TestEqual(TEXT("final medical damage earns only finisher bonus"), Attacker.Value(), 32.0f);
	Defender.Pawn->Team = 0;
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("friendly damage cannot charge"), Attacker.Value(), 32.0f);
	Defender.Pawn->Team = 1;
	Defender.Player->ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetHealthAttribute(), 5.0f);
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("fatal damage still earns its hit"), Attacker.Value(), 34.0f);
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("already dead target cannot charge"), Attacker.Value(), 34.0f);
	FFixture::ResetHealth(*Defender.Player->ASC);
	Defender.Player->ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetMaxShieldAttribute(), 10.0f);
	Defender.Player->ASC->SetNumericAttributeBase(UVBHealthAttributeSet::GetShieldAttribute(), 10.0f);
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("fully absorbed shield hit counts"), Attacker.Value(), 36.0f);
	TestEqual(TEXT("shield-only hit leaves health intact"),
		Defender.Player->ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 100.0f);
	UGameplayEffect* Heal = NewObject<UGameplayEffect>();
	Heal->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo& Healing = Heal->Modifiers.AddDefaulted_GetRef();
	Healing.Attribute = UVBHealthAttributeSet::GetIncomingHealingAttribute();
	Healing.ModifierOp = EGameplayModOp::Additive;
	Healing.ModifierMagnitude = FScalableFloat(10.0f);
	bool bHealed = false;
	UAbilitySystemComponent& Victim = *Defender.Player->ASC;
	const FDelegateHandle Reentry = Victim.GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute()).AddLambda(
		[&](const FOnAttributeChangeData& Data)
		{
			if (bHealed || Data.NewValue >= Data.OldValue)
			{
				return;
			}

			bHealed = true;
			Victim.ApplyGameplayEffectToSelf(Heal, 1.0f, Victim.MakeEffectContext());
		});
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	Victim.GetGameplayAttributeValueChangeDelegate(UVBHealthAttributeSet::GetHealthAttribute()).Remove(Reentry);
	TestTrue(TEXT("health callback actually applied nested healing"), bHealed);
	TestEqual(TEXT("nested healing restores health"), Victim.GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 100.0f);
	TestEqual(TEXT("nested healing cannot erase the accepted outer hit credit"), Attacker.Value(), 38.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeSourceAvatarTest, "VB.Unit.CinematicSkillGauge.SourceAvatar", VBGaugeTest::Flags)
bool FVBGaugeSourceAvatarTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture Attacker, Defender;
	if (!Attacker.Init(*this) || !Defender.Init(*this))
	{
		return false;
	}

	Defender.Pawn->Team = 1;
	UAbilitySystemComponent& ASC = *Attacker.Player->ASC;
	FGameplayEffectContextHandle OldContext(new FVBGameplayEffectContext());
	OldContext.AddInstigator(Attacker.Player, Attacker.Pawn);
	FVBGameplayEffectContext::Get(OldContext)->SetChargeSource(EVBCombatChargeSource::OrdinaryAttack);
	FGameplayEffectSpecHandle OldStrike = ASC.MakeOutgoingSpec(UVBGaugeTestDamage::StaticClass(), 1.0f, OldContext);
	if (!TestTrue(TEXT("old strike prepared before replacement"), OldStrike.IsValid()))
	{
		return false;
	}

	AVBGaugeTestPawn* OldPawn = Attacker.Pawn;
	AVBGaugeTestPawn* Replacement = Attacker.Isolated.World->SpawnActor<AVBGaugeTestPawn>();
	if (!TestNotNull(TEXT("new attacker body"), Replacement))
	{
		return false;
	}

	Replacement->ASC = &ASC;
	Replacement->Weapons->SetFixtureOwned(true);
	ASC.InitAbilityActorInfo(Attacker.Player, Replacement);
	Attacker.Player->OnPawnSet.Broadcast(Attacker.Player, Replacement, OldPawn);
	ASC.ApplyGameplayEffectSpecToTarget(*OldStrike.Data.Get(), Defender.Player->ASC);
	TestEqual(TEXT("old body's delayed strike cannot charge its replacement"), Attacker.Value(), 0.0f);
	TestEqual(TEXT("old strike actually resolved damage"),
		Defender.Player->ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()), 90.0f);
	Attacker.Pawn = Replacement;
	Attacker.Hit(Defender, EVBCombatChargeSource::OrdinaryAttack);
	TestEqual(TEXT("new body's own strike earns normally"), Attacker.Value(), 2.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeSaveTest, "VB.Unit.CinematicSkillGauge.Save", VBGaugeTest::Flags)
bool FVBGaugeSaveTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FVBIsolatedGameWorld F;
	if (!F.Init(*this))
	{
		return false;
	}

	AVBPlayerState* Player = F.World->SpawnActor<AVBPlayerState>();
	if (!TestNotNull(TEXT("actual saved resource owner"), Player))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *Player->GetAbilitySystemComponent();
	ASC.SetNumericAttributeBase(Maximum(), 100.0f);
	ASC.SetNumericAttributeBase(Current(), 75.0f);
	UVBSaveGame* Save = NewObject<UVBSaveGame>();
	Player->SnapshotBuild(Save->PlayerBuild);
	TestEqual(TEXT("snapshot saves current"), Save->PlayerBuild.AttributeValues.FindRef(TEXT("CinematicSkillGauge")), 75.0f);
	ASC.SetNumericAttributeBase(Maximum(), 60.0f);
	ASC.SetNumericAttributeBase(Current(), 0.0f);
	FVBSaveContext Context;
	Context.World = F.World;
	Context.PlayerState = Player;
	UVBSaveParticipantSubsystem* Registry = F.World->GetSubsystem<UVBSaveParticipantSubsystem>();
	if (!TestNotNull(TEXT("save registry"), Registry))
	{
		return false;
	}

	Registry->ReadAll(*Save, Context);
	TestEqual(TEXT("stage3 restore clamps current to authored cap"), ASC.GetNumericAttribute(Current()), 60.0f);
	TestEqual(TEXT("restore cannot override cap"), ASC.GetNumericAttribute(Maximum()), 60.0f);
	Save->PlayerBuild.AttributeValues.Remove(TEXT("CinematicSkillGauge"));
	AVBPlayerState* Fresh = F.World->SpawnActor<AVBPlayerState>();
	if (!TestNotNull(TEXT("fresh legacy-load player"), Fresh))
	{
		return false;
	}

	Fresh->GetAbilitySystemComponent()->SetNumericAttributeBase(Maximum(), 100.0f);
	Context.PlayerState = Fresh;
	Registry->ReadAll(*Save, Context);
	TestEqual(TEXT("legacy slot starts empty on fresh state"), Fresh->GetAbilitySystemComponent()->GetNumericAttribute(Current()), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeRulesTest, "VB.Unit.CinematicSkillGauge.Rules", VBGaugeTest::Flags)
bool FVBGaugeRulesTest::RunTest(const FString& Parameters)
{
	UVBCinematicSkillChargeRules* Rules = NewObject<UVBCinematicSkillChargeRules>();
	TestFalse(TEXT("missing rules fail loudly in authoring validation"), Rules->ValidateSetup().IsEmpty());
	Rules->SkillDefinition = NewObject<UVBCinematicSkillDefinition>(Rules);
	Rules->GainEffect = UVBGaugeTestGain::StaticClass();
	TestTrue(TEXT("valid data"), Rules->ValidateSetup().IsEmpty());
	Rules->NormalHitGain = -1;
	TestFalse(TEXT("negative rate invalid"), Rules->ValidateSetup().IsEmpty());
	Rules->NormalHitGain = 2;
	Rules->GainEffect = UVBGaugeTestCost::StaticClass();
	TestFalse(TEXT("wrong gain effect invalid"), Rules->ValidateSetup().IsEmpty());
	Rules->GainEffect = UVBGaugeTestGain::StaticClass();
	Rules->RecentCreditLimit = 0;
	TestFalse(TEXT("unbounded or zero receipt cache invalid"), Rules->ValidateSetup().IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeCostTest, "VB.Unit.CinematicSkillGauge.Cost", VBGaugeTest::Flags)
bool FVBGaugeCostTest::RunTest(const FString& Parameters)
{
	using namespace VBGaugeTest;
	FFixture F;
	if (!F.Init(*this, false))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Player->ASC;
	// 여기선 스킬의 실제 입장 판정이 돌므로 필요한 stance 를 갖춰 두고, 아래에선 게이지만 다르게 한다
	F.Pawn->Weapons->SetFixtureSelected(EVBWeaponType::Fighter);
	const FGameplayAbilitySpecHandle Handle = ASC.GiveAbility(FGameplayAbilitySpec(UVBGaugeTestSkill::StaticClass()));
	ASC.SetNumericAttributeBase(Current(), 99.0f);
	TestFalse(TEXT("Override0 alone is affordable but explicit full check refuses"), ASC.TryActivateAbility(Handle));
	TestEqual(TEXT("refused use spends nothing"), F.Value(), 99.0f);
	ASC.SetNumericAttributeBase(Current(), 100.0f);
	TestTrue(TEXT("full skill commits"), ASC.TryActivateAbility(Handle));
	TestEqual(TEXT("standard cost consumes all"), F.Value(), 0.0f);
	ASC.CancelAbilityHandle(Handle);
	TestEqual(TEXT("post-commit cancel has no refund"), F.Value(), 0.0f);
	ASC.SetNumericAttributeBase(Maximum(), 120.0f);
	ASC.SetNumericAttributeBase(Current(), 120.0f);
	TestTrue(TEXT("non-default full cap commits"), ASC.TryActivateAbility(Handle));
	TestEqual(TEXT("no hardcoded100 remainder"), F.Value(), 0.0f);
	ASC.CancelAbilityHandle(Handle);
	TestFalse(TEXT("missing cost is invalid"), UVBCinematicSkillChargeRules::ValidateCost(nullptr).IsEmpty());
	TestFalse(TEXT("gain GE cannot be cost"), UVBCinematicSkillChargeRules::ValidateCost(GetDefault<UVBGaugeTestGain>()).IsEmpty());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGaugeSourceContextTest, "VB.Unit.CinematicSkillGauge.SourceContext", VBGaugeTest::Flags)
bool FVBGaugeSourceContextTest::RunTest(const FString& Parameters)
{
	FVBGameplayEffectContext Context;
	Context.SetChargeSource(EVBCombatChargeSource::CinematicSkill);
	const TUniquePtr<FVBGameplayEffectContext> Copy(Context.Duplicate());
	TestTrue(TEXT("local duplicate preserves immutable origin"), Copy->GetChargeSource() == EVBCombatChargeSource::CinematicSkill);
	TArray<uint8> Bytes;
	FMemoryWriter Writer(Bytes);
	bool bSuccess = false;
	Context.NetSerialize(Writer, nullptr, bSuccess);
	TestTrue(TEXT("context write"), bSuccess);
	FMemoryReader Reader(Bytes);
	FVBGameplayEffectContext Loaded;
	Loaded.SetChargeSource(EVBCombatChargeSource::OrdinaryAttack);
	Loaded.NetSerialize(Reader, nullptr, bSuccess);
	TestTrue(TEXT("context read"), bSuccess);
	TestTrue(TEXT("wire cannot supply a combat-credit origin"), Loaded.GetChargeSource() == EVBCombatChargeSource::Unspecified);
	return true;
}
#endif
