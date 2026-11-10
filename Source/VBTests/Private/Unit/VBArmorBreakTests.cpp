// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Abilities/VBGA_HitReact.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Data/VBCombatRuleTable.h"
#include "Data/VBCombatSettings.h"
#include "Engine/World.h"
#include "GameFramework/Character.h"
#include "GameplayEffect.h"
#include "Misc/ScopeExit.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

// 갑옷 깨기 - 깨지는 갑옷은 Poise 를 비우는 공격에 무너지고, 보호 갑옷은 규칙 B 를 지킨다.
// 실제 poise 타격은 PostGameplayEffectExecute 를 거치고 깨짐은 실제 이벤트로 센다.
// "안 깨짐" 만 보면 아무것도 못 깨는 경로도 통과하므로 양쪽을 다 잰다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBArmorBreakTest, "VB.Unit.Combat.ArmorBreak",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)

bool FVBArmorBreakTest::RunTest(const FString& Parameters)
{
	const UWorld::InitializationValues Init = UWorld::InitializationValues()
		.AllowAudioPlayback(false).CreatePhysicsScene(false).CreateNavigation(false).CreateAISystem(false);
	UWorld* World = UWorld::CreateWorld(EWorldType::Game, false, NAME_None, nullptr, true, ERHIFeatureLevel::Num, &Init);
	if (!TestNotNull(TEXT("world"), World))
	{
		return false;
	}

	const TStrongObjectPtr<UPackage> OwnedPackage(World->GetPackage());
	ON_SCOPE_EXIT
	{
		World->DestroyWorld(false);
		OwnedPackage->SetDirtyFlag(false);
	};

	ACharacter* Pawn = World->SpawnActor<ACharacter>();
	if (!TestNotNull(TEXT("pawn"), Pawn))
	{
		return false;
	}

	UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Pawn);
	Pawn->AddInstanceComponent(ASC);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Pawn, Pawn);
	UVBCombatAttributeSet* Combat = NewObject<UVBCombatAttributeSet>(Pawn);
	ASC->AddAttributeSetSubobject(Combat);

	// GE_PhysicalDamage 의 poise 줄과 같은 모양 - Data.PoiseDamage SetByCaller 를 IncomingPoiseDamage 로 넣는다.
	const TStrongObjectPtr<UGameplayEffect> PoiseHit(NewObject<UGameplayEffect>(GetTransientPackage()));
	PoiseHit->DurationPolicy = EGameplayEffectDurationType::Instant;
	FGameplayModifierInfo Modifier;
	Modifier.Attribute = UVBCombatAttributeSet::GetIncomingPoiseDamageAttribute();
	Modifier.ModifierOp = EGameplayModOp::Additive;
	FSetByCallerFloat ByCaller;
	ByCaller.DataTag = VBGameplayTags::Data_PoiseDamage;
	Modifier.ModifierMagnitude = FGameplayEffectModifierMagnitude(ByCaller);
	PoiseHit->Modifiers.Add(Modifier);

	int32 Breaks = 0;
	const FDelegateHandle BreakHandle = ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_PoiseBroken)
		.AddLambda([&Breaks](const FGameplayEventData*) { ++Breaks; });
	ON_SCOPE_EXIT { ASC->GenericGameplayEventCallbacks.FindOrAdd(VBGameplayTags::Event_Combat_PoiseBroken).Remove(BreakHandle); };

	auto Poise = [ASC] { return ASC->GetNumericAttribute(UVBCombatAttributeSet::GetPoiseAttribute()); };
	auto Reset = [&](float MaxPoise, float StartPoise)
	{
		ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxPoiseAttribute(), MaxPoise);
		ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetPoiseAttribute(), StartPoise);
		Breaks = 0;
	};
	auto Hit = [&](float PoiseDamage)
	{
		FGameplayEffectSpec Spec(PoiseHit.Get(), ASC->MakeEffectContext(), 1.0f);
		Spec.SetSetByCallerMagnitude(VBGameplayTags::Data_PoiseDamage, PoiseDamage);
		ASC->ApplyGameplayEffectSpecToSelf(Spec);
	};
	const FGameplayTag Armor = VBGameplayTags::State_Combat_SuperArmor;
	const FGameplayTag Breakable = VBGameplayTags::State_Combat_SuperArmor_Breakable;

	// 자식 태그도 실제 관문에서 SuperArmor 로 읽혀야 한다 - combat rule table 이 UVBGA_HitReact 시작을 막는 상태들.
	const UVBCombatRuleTable* Rules = GetDefault<UVBCombatSettings>()->CombatRules.LoadSynchronous();
	if (!TestNotNull(TEXT("shipped combat rule table"), Rules))
	{
		return false;
	}

	FGameplayTagContainer HitReactBlocked;
	FGameplayTagContainer HitReactRequired;
	Rules->AppendActivationTags(GetDefault<UVBGA_HitReact>()->GetAssetTags(), HitReactBlocked, HitReactRequired);
	TestTrue(TEXT("HitReact is blocked by SuperArmor"), HitReactBlocked.HasTagExact(Armor));
	TestFalse(TEXT("no armor: HitReact gate open"), ASC->HasAnyMatchingGameplayTags(HitReactBlocked));
	ASC->AddLooseGameplayTag(Breakable);
	TestTrue(TEXT("breakable armor matches SuperArmor"), ASC->HasMatchingGameplayTag(Armor));
	TestTrue(TEXT("breakable armor blocks HitReact"), ASC->HasAnyMatchingGameplayTags(HitReactBlocked));

	// P1 깨지는 갑옷 + 0 을 넘는 타격 -> 바로 깨지고 다시 채워진다.
	Reset(100.0f, 30.0f);
	Hit(40.0f);
	TestEqual(TEXT("P1 hit crossing 0 breaks breakable armor once"), Breaks, 1);
	TestEqual(TEXT("P1 poise refilled"), Poise(), 100.0f);

	// P2 게이지가 이미 0 (앞서 보호 갑옷 아래서 비었음) -> 다음 타격에 깨진다.
	Reset(100.0f, 0.0f);
	Hit(10.0f);
	TestEqual(TEXT("P2 hit at an empty gauge breaks breakable armor once"), Breaks, 1);
	TestEqual(TEXT("P2 poise refilled"), Poise(), 100.0f);

	// N4 빈 게이지에 poise 피해 0 -> 깨지지 않는다.
	Reset(100.0f, 0.0f);
	Hit(0.0f);
	TestEqual(TEXT("N4 zero poise damage never breaks"), Breaks, 0);
	TestEqual(TEXT("N4 poise stays 0"), Poise(), 0.0f);

	// P3 0 에 닿지 않는 타격 -> 깨지지 않고 압박만 쌓인다.
	Reset(100.0f, 100.0f);
	Hit(40.0f);
	TestEqual(TEXT("P3 hit above 0 does not break"), Breaks, 0);
	TestEqual(TEXT("P3 poise reduced"), Poise(), 60.0f);

	// N3 깨지는 갑옷과 보호 갑옷이 함께 있으면 보호 쪽이 이긴다.
	ASC->AddLooseGameplayTag(Armor);
	Reset(100.0f, 30.0f);
	Hit(40.0f);
	TestEqual(TEXT("N3 mixed armor keeps rule B"), Breaks, 0);
	TestEqual(TEXT("N3 poise held at 0"), Poise(), 0.0f);
	ASC->RemoveLooseGameplayTag(Breakable);

	// N2 보호 갑옷만 (쓰러짐 휘두르기, 페이즈 전환) -> 규칙 B, 0 에서 멈춘다.
	Reset(100.0f, 30.0f);
	Hit(40.0f);
	TestEqual(TEXT("N2 protected armor defers the break"), Breaks, 0);
	TestEqual(TEXT("N2 poise held at 0"), Poise(), 0.0f);
	ASC->RemoveLooseGameplayTag(Armor);
	TestFalse(TEXT("armor fully removed"), ASC->HasMatchingGameplayTag(Armor));

	// N5 갑옷 없음 - 그대로, 0 에 닿는 poise 타격은 한 번 깨진다.
	Reset(100.0f, 30.0f);
	Hit(40.0f);
	TestEqual(TEXT("N5 unarmored hit breaks once"), Breaks, 1);

	// N6 깨지는 갑옷 아래 MaxPoise 0 (플레이어) -> poise 체계가 꺼져 아무 일도 없다.
	ASC->AddLooseGameplayTag(Breakable);
	Reset(0.0f, 0.0f);
	Hit(40.0f);
	TestEqual(TEXT("N6 actor without poise never breaks"), Breaks, 0);
	ASC->RemoveLooseGameplayTag(Breakable);
	return true;
}
#endif
