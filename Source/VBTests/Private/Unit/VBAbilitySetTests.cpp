// ProjectVB Copyright (c) 2025. All Rights Reserved.

// UVBAbilitySet 은 능력, 효과를 주는 유일한 길이고, 그 receipt 는 준 것만 정확히 거둔다.

#include "Misc/AutomationTest.h"

#include "Unit/VBAbilitySetTestEffects.h"
#include "AbilitySystem/VBAbilitySet.h"
#include "AbilitySystemComponent.h"
#include "Abilities/GameplayAbility.h"
#include "Abilities/GameplayAbility_CharacterJump.h"
#include "AssetRegistry/AssetData.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Enemy/VBEnemyBase.h"
#include "Blueprint/BlueprintSupport.h"
#include "Engine/Blueprint.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"
#include "Misc/PackageName.h"
#include "Misc/ScopeExit.h"
#include "Player/VBPlayerState.h"
#include "UObject/Package.h"
#include "UObject/StrongObjectPtr.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilitySetGrantAndTakeTest, "VB.Unit.AbilitySet.GrantAndTake",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAbilitySetGrantAndTakeTest::RunTest(const FString& Parameters)
{
	using namespace VBAbilitySetTest;

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
	World->InitializeActorsForPlay(FURL());

	auto MakeASC = [](AActor* Owner)
	{
		UAbilitySystemComponent* ASC = NewObject<UAbilitySystemComponent>(Owner);
		Owner->AddInstanceComponent(ASC);
		ASC->RegisterComponent();
		ASC->InitAbilityActorInfo(Owner, Owner);
		return ASC;
	};
	AActor* OwnerA = World->SpawnActor<AActor>();
	AActor* OwnerB = World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("owner A"), OwnerA) || !TestNotNull(TEXT("owner B"), OwnerB))
	{
		return false;
	}

	UAbilitySystemComponent* ASCA = MakeASC(OwnerA);
	UAbilitySystemComponent* ASCB = MakeASC(OwnerB);
	UObject* const Source = OwnerA;

	const TStrongObjectPtr<UVBAbilitySet> Set(NewObject<UVBAbilitySet>(GetTransientPackage()));
	Fill(*Set,
		{Ability(UGameplayAbility::StaticClass()), Ability(UGameplayAbility_CharacterJump::StaticClass(), 2)},
		{Effect(UVBAbilitySetTestInfiniteEffect::StaticClass()), Effect(UVBAbilitySetTestInstantEffect::StaticClass())});

	auto ActiveEffects = [](const UAbilitySystemComponent* ASC) { return ASC->GetActiveGameplayEffects().GetNumGameplayEffects(); };

	FVBAbilitySetGrantedHandles ReceiptA;
	FVBAbilitySetGrantedHandles ReceiptB;
	Set->GiveToAbilitySystem(ASCA, ReceiptA, Source);
	Set->GiveToAbilitySystem(ASCB, ReceiptB, OwnerB);

	// 한 번 준 뒤 ASC 에 남는 것 - set 순서의 능력(level, source 포함)과 활성 효과 하나(instant 는 실행만 됨).
	const TArray<FGameplayAbilitySpec>& SpecsA = ASCA->GetActivatableAbilities();
	if (TestEqual(TEXT("two specs on A"), SpecsA.Num(), 2))
	{
		TestTrue(TEXT("first spec is the first entry"), SpecsA[0].Ability
			&& SpecsA[0].Ability->GetClass() == UGameplayAbility::StaticClass());
		TestTrue(TEXT("second spec is the second entry"), SpecsA[1].Ability
			&& SpecsA[1].Ability->GetClass() == UGameplayAbility_CharacterJump::StaticClass());
		TestEqual(TEXT("second spec level"), SpecsA[1].Level, 2);
		TestTrue(TEXT("spec source object is the one passed"), SpecsA[0].SourceObject.Get() == Source);
	}

	TestEqual(TEXT("one active effect on A"), ActiveEffects(ASCA), 1);
	TestFalse(TEXT("A receipt holds the grant"), ReceiptA.IsEmpty());

	// A 를 거두면 A 의 receipt 것만 빠지고 B 는 제 것을 지킨다.
	ReceiptA.TakeFromAbilitySystem();
	TestEqual(TEXT("A has no ability after take"), ASCA->GetActivatableAbilities().Num(), 0);
	TestEqual(TEXT("A has no active effect after take"), ActiveEffects(ASCA), 0);
	TestTrue(TEXT("A receipt is empty after take"), ReceiptA.IsEmpty());
	TestEqual(TEXT("B still has its two abilities"), ASCB->GetActivatableAbilities().Num(), 2);
	TestEqual(TEXT("B still has its active effect"), ActiveEffects(ASCB), 1);

	// 두 번째 거두기는 아무 일도 안 한다.
	ReceiptA.TakeFromAbilitySystem();
	TestTrue(TEXT("A receipt stays empty after a second take"), ReceiptA.IsEmpty());
	TestEqual(TEXT("B unchanged by A's second take"), ASCB->GetActivatableAbilities().Num(), 2);

	ReceiptB.TakeFromAbilitySystem();
	TestEqual(TEXT("B has no ability after its take"), ASCB->GetActivatableAbilities().Num(), 0);

	// instant 효과만 실행한 grant 도 세므로 같은 receipt 에 두 번 주면 잡힌다.
	const TStrongObjectPtr<UVBAbilitySet> InstantOnly(MakeSet(GetTransientPackage(), {}, {Effect(UVBAbilitySetTestInstantEffect::StaticClass())}));
	FVBAbilitySetGrantedHandles ReceiptInstant;
	InstantOnly->GiveToAbilitySystem(ASCB, ReceiptInstant, OwnerB);
	TestFalse(TEXT("an instant-only grant is not an empty receipt"), ReceiptInstant.IsEmpty());
	TestEqual(TEXT("an instant-only grant leaves no active effect"), ActiveEffects(ASCB), 0);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilitySetValidationTest, "VB.Unit.AbilitySet.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAbilitySetValidationTest::RunTest(const FString& Parameters)
{
	using namespace VBAbilitySetTest;

	TestEqual(TEXT("a clean set has no problem"), UVBAbilitySet::FindProblems(
		{Ability(UGameplayAbility::StaticClass()), Ability(UGameplayAbility_CharacterJump::StaticClass())},
		{Effect(UVBAbilitySetTestInfiniteEffect::StaticClass())}).Num(), 0);
	TestEqual(TEXT("an empty ability entry"), UVBAbilitySet::FindProblems({Ability(nullptr)}, {}).Num(), 1);
	TestEqual(TEXT("the same ability twice"), UVBAbilitySet::FindProblems(
		{Ability(UGameplayAbility::StaticClass()), Ability(UGameplayAbility::StaticClass())}, {}).Num(), 1);
	TestEqual(TEXT("ability level 0"), UVBAbilitySet::FindProblems({Ability(UGameplayAbility::StaticClass(), 0)}, {}).Num(), 1);
	TestEqual(TEXT("an empty effect entry"), UVBAbilitySet::FindProblems({}, {Effect(nullptr)}).Num(), 1);
	TestEqual(TEXT("effect level 0"), UVBAbilitySet::FindProblems({}, {Effect(UVBAbilitySetTestInfiniteEffect::StaticClass(), 0.0f)}).Num(), 1);
	return true;
}

// 실제 player state 와 적 blueprint 는 모두 무언가 주고 검증을 통과하는 set 을 가리킨다.
//   없으면 능력도 스탯도 없이 시작하는데 runtime 은 경고만 한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAbilitySetShippedSetsTest, "VB.Unit.AbilitySet.ShippedSets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBAbilitySetShippedSetsTest::RunTest(const FString& Parameters)
{
	auto CheckSet = [this](const FString& Owner, const UVBAbilitySet* Set)
	{
		if (!TestNotNull(*FString::Printf(TEXT("%s names a set"), *Owner), Set))
		{
			return;
		}

		TestTrue(*FString::Printf(TEXT("%s set grants abilities"), *Owner), Set->GetGrantedAbilities().Num() > 0);
		TestEqual(*FString::Printf(TEXT("%s set problems"), *Owner),
			UVBAbilitySet::FindProblems(Set->GetGrantedAbilities(), Set->GetGrantedEffects()).Num(), 0);
	};

	const UClass* PlayerStateClass = LoadClass<AVBPlayerState>(nullptr,
		TEXT("/Game/VB/Blueprints/GameModes/BP_VBPlayerState.BP_VBPlayerState_C"));
	if (TestNotNull(TEXT("BP_VBPlayerState"), PlayerStateClass))
	{
		CheckSet(TEXT("BP_VBPlayerState"), GetDefault<AVBPlayerState>(const_cast<UClass*>(PlayerStateClass))->StartupAbilitySet);
	}

	IAssetRegistry& Registry = IAssetRegistry::GetChecked();
	Registry.SearchAllAssets(true);
	TArray<FAssetData> Blueprints;
	Registry.GetAssetsByPath(TEXT("/Game/VB"), Blueprints, true);
	int32 EnemyCount = 0;
	for (const FAssetData& Asset : Blueprints)
	{
		FString NativeParent;
		if (!Asset.GetTagValue(FBlueprintTags::NativeParentClassPath, NativeParent))
		{
			continue;
		}

		const UClass* Native = FindObject<UClass>(nullptr, *FPackageName::ExportTextPathToObjectPath(NativeParent));
		if (!Native || !Native->IsChildOf(AVBEnemyBase::StaticClass()))
		{
			continue;
		}

		const UBlueprint* Blueprint = Cast<UBlueprint>(Asset.GetAsset());
		if (!TestTrue(*FString::Printf(TEXT("%s loads"), *Asset.AssetName.ToString()), Blueprint && Blueprint->GeneratedClass))
		{
			continue;
		}

		++EnemyCount;
		CheckSet(Asset.AssetName.ToString(), GetDefault<AVBEnemyBase>(Blueprint->GeneratedClass)->AbilitySet);
	}

	TestTrue(TEXT("found enemy blueprints"), EnemyCount > 0);
	return true;
}
