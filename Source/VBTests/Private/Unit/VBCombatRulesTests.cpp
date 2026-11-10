// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "VBCinematicSkillTestTypes.h"
#include "VBIsolatedGameWorld.h"
#include "AssetRegistry/AssetData.h"
#include "Data/VBCombatRuleTable.h"
#include "Data/VBCombatSettings.h"
#include "Misc/DataValidation.h"
#include "VBTestAbilityTags.h"
#include "AssetRegistry/IAssetRegistry.h"
#include "Engine/Blueprint.h"
#include "GameplayTagContainer.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "UObject/Package.h"
#include "UObject/UObjectIterator.h"

// 전투 규칙 행렬: 게임이 줄 수 있는 ability(native VB 클래스, BP_GA_*)마다 어떤 상태, 실행 중 ability 가 시작을 막고 무엇이 취소하는지.
//   git 에 둔 golden 파일과 비교한다 - 다르거나 없으면 실패하고 옆에 .actual.txt 를 쓴다.
//   의도한 규칙 변경일 때만 그것을 golden 으로 복사한다.
namespace VBCombatRulesTest
{
	const TCHAR* GoldenRelativePath = TEXT("Source/VBTests/Golden/CombatRulesMatrix.txt");

	struct FAbilityTags
	{
		FString Name;
		FGameplayTagContainer Identity;
		FGameplayTagContainer Owned;
		FGameplayTagContainer Blocked;
		FGameplayTagContainer Required;
		FGameplayTagContainer BlocksWhileRunning;
		FGameplayTagContainer CancelsOnStart;
	};

	// getter 없는 protected 엔진 멤버라 reflection 으로 읽는다 - 게임 코드에 시험용 문을 내지 않는다.
	FGameplayTagContainer ReadContainer(const UGameplayAbility& Ability, const FName Property)
	{
		const FStructProperty* Prop = FindFProperty<FStructProperty>(UGameplayAbility::StaticClass(), Property);
		check(Prop && Prop->Struct == FGameplayTagContainer::StaticStruct());
		return *Prop->ContainerPtrToValuePtr<FGameplayTagContainer>(&Ability);
	}

	// 클래스가 직접 가진 관계 container - 표가 유일한 집이라 비어 있어야 한다(NoRelationsInGAs).
	const TCHAR* RelationContainers[] = { TEXT("ActivationBlockedTags"), TEXT("ActivationRequiredTags"), TEXT("BlockAbilitiesWithTag"),
		TEXT("CancelAbilitiesWithTag") };

	const UVBCombatRuleTable* ShippedTable(FAutomationTestBase& Test)
	{
		const UVBCombatRuleTable* Table = GetDefault<UVBCombatSettings>()->CombatRules.LoadSynchronous();
		Test.TestNotNull(TEXT("Project Settings > VB Combat > CombatRules resolves"), Table);
		return Table;
	}

	// 실효 관계 = 엔진이 보는 것: 클래스 자신의 container + 신원에 대한 표의 답. hook 과 같은 조회를 쓴다
	//   (UVBGameplayAbility::DoesAbilitySatisfyTagRequirements, UVBAbilitySystemComponent::ApplyAbilityBlockAndCancelTags).
	FAbilityTags Read(const UClass& Class, const UVBCombatRuleTable& Table)
	{
		const UGameplayAbility& CDO = *Class.GetDefaultObject<UGameplayAbility>();
		FAbilityTags Tags;
		Tags.Name = Class.GetName();
		Tags.Identity = CDO.GetAssetTags();
		Tags.Owned = ReadContainer(CDO, TEXT("ActivationOwnedTags"));
		Tags.Blocked = ReadContainer(CDO, TEXT("ActivationBlockedTags"));
		Tags.Required = ReadContainer(CDO, TEXT("ActivationRequiredTags"));
		Tags.BlocksWhileRunning = ReadContainer(CDO, TEXT("BlockAbilitiesWithTag"));
		Tags.CancelsOnStart = ReadContainer(CDO, TEXT("CancelAbilitiesWithTag"));
		Table.AppendActivationTags(Tags.Identity, Tags.Blocked, Tags.Required);
		Table.AppendBlockAndCancelTags(Tags.Identity, Tags.BlocksWhileRunning, Tags.CancelsOnStart);
		return Tags;
	}

	// native VB ability(게임 모듈만, 시험 대역 제외)와 /Game/VB 아래 모든 Blueprint ability 클래스.
	TArray<const UClass*> GatherAbilityClasses(FAutomationTestBase& Test)
	{
		TArray<const UClass*> Classes;
		for (TObjectIterator<UClass> It; It; ++It)
		{
			const UClass* Class = *It;
			if (Class->IsChildOf(UGameplayAbility::StaticClass())
				&& !Class->HasAnyClassFlags(CLASS_Abstract | CLASS_Deprecated | CLASS_NewerVersionExists)
				&& Class->GetPackage()->GetName() == TEXT("/Script/VB"))
			{
				Classes.Add(Class);
			}
		}
		// 부모 클래스로 찾는다 - 이름 필터는 다르게 이름 붙은 ability BP 를 놓친다.
		IAssetRegistry& Registry = IAssetRegistry::GetChecked();
		Registry.ScanPathsSynchronous({ TEXT("/Game/VB") }, /*bForceRescan=*/false);
		TSet<FTopLevelAssetPath> Derived;
		Registry.GetDerivedClassNames({ UGameplayAbility::StaticClass()->GetClassPathName() }, {}, Derived);
		for (const FTopLevelAssetPath& Path : Derived)
		{
			if (!Path.GetPackageName().ToString().StartsWith(TEXT("/Game/VB/")))
			{
				continue;
			}
			// registry 는 로드된 BP 클래스의 에디터 메모리 사본(SKEL_, REINST_)도 내놓는다 - ability 가 아니다.
			const FString AssetName = Path.GetAssetName().ToString();
			if (AssetName.StartsWith(TEXT("SKEL_")) || AssetName.StartsWith(TEXT("REINST_")))
			{
				continue;
			}

			const FString ClassPath = Path.ToString();
			const UClass* Class = LoadObject<UClass>(nullptr, *ClassPath);
			if (!Class || !Class->IsChildOf(UGameplayAbility::StaticClass()))
			{
				Test.AddError(FString::Printf(TEXT("%s did not load as an ability class"), *ClassPath));
				continue;
			}

			Classes.AddUnique(Class);
		}

		return Classes;
	}

	TArray<FAbilityTags> GatherAbilities(FAutomationTestBase& Test, const UVBCombatRuleTable& Table)
	{
		TArray<FAbilityTags> Abilities;
		for (const UClass* Class : GatherAbilityClasses(Test))
		{
			Abilities.Add(Read(*Class, Table));
		}

		Abilities.Sort([](const FAbilityTags& A, const FAbilityTags& B) { return A.Name < B.Name; });
		return Abilities;
	}

	// 칸마다 정렬된 한 줄 - 디자이너가 애셋을 고치면 그 줄의 git diff 로 보인다.
	FString ExportTable(const UVBCombatRuleTable& Table)
	{
		TArray<FString> Lines;
		for (const FVBStateRule& Rule : Table.StateRules)
		{
			for (const FGameplayTag& Tag : Rule.BlocksStart)
			{
				Lines.Add(FString::Printf(TEXT("state %s blocks-start %s"), *Rule.State.ToString(), *Tag.ToString()));
			}

			for (const FGameplayTag& Tag : Rule.CancelsOnEntry)
			{
				Lines.Add(FString::Printf(TEXT("state %s cancels-on-entry %s"), *Rule.State.ToString(), *Tag.ToString()));
			}

			Lines.Add(FString::Printf(TEXT("state %s why %s"), *Rule.State.ToString(), *Rule.Why.ReplaceCharWithEscapedChar()));
		}

		for (const FVBAbilityRule& Rule : Table.AbilityRules)
		{
			for (const FGameplayTag& Tag : Rule.BlocksWhileRunning)
			{
				Lines.Add(FString::Printf(TEXT("ability %s blocks-while-running %s"), *Rule.Ability.ToString(), *Tag.ToString()));
			}

			for (const FGameplayTag& Tag : Rule.CancelsOnStart)
			{
				Lines.Add(FString::Printf(TEXT("ability %s cancels-on-start %s"), *Rule.Ability.ToString(), *Tag.ToString()));
			}

			for (const FGameplayTag& Tag : Rule.RequiresState)
			{
				Lines.Add(FString::Printf(TEXT("ability %s requires-state %s"), *Rule.Ability.ToString(), *Tag.ToString()));
			}

			Lines.Add(FString::Printf(TEXT("ability %s why %s"), *Rule.Ability.ToString(), *Rule.Why.ReplaceCharWithEscapedChar()));
		}

		Lines.Sort();
		return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}

	// golden 비교: 같으면 통과하고 낡은 .actual.txt 를 지우고, 다르거나 없으면 실패하고 실제 값을 옆에 쓴다.
	void CompareGolden(FAutomationTestBase& Test, const TCHAR* RelativePath, const FString& Actual, const TCHAR* What)
	{
		const FString Golden = FPaths::Combine(FPaths::ProjectDir(), RelativePath);
		const FString ActualPath = FPaths::ChangeExtension(Golden, TEXT("actual.txt"));
		FString Expected;
		if (FFileHelper::LoadFileToString(Expected, *Golden) && Expected.Replace(TEXT("\r\n"), TEXT("\n")) == Actual)
		{
			IFileManager::Get().Delete(*ActualPath, /*RequireExists=*/false, /*EvenReadOnly=*/true, /*Quiet=*/true);
			return;
		}

		IFileManager::Get().MakeDirectory(*FPaths::GetPath(ActualPath), /*Tree=*/true);
		FFileHelper::SaveStringToFile(Actual, *ActualPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM);
		Test.AddError(FString::Printf(TEXT("%s differs from %s (or it is missing); actual written to %s - diff them, and copy only for an intended rule change"),
			What, *Golden, *ActualPath));
	}

	// 엔진 의미 그대로: 가진 태그는 부모까지 펼쳐 ActivationBlockedTags 와 맞추고(DoesAbilitySatisfyTagRequirements HasAny),
	//   실행 중 ability 의 막기, 취소는 후보 asset tag 를 부모까지 맞춘다(AreAbilityTagsBlocked / CancelAbilities).
	//   상태 줄의 CancelsOnEntry 는 UVBAbilitySystemComponent::OnTagUpdated 와 같이 읽는다.
	FString BuildMatrix(const TArray<FAbilityTags>& Abilities, const UVBCombatRuleTable& Table)
	{
		FGameplayTagContainer States;
		for (const FAbilityTags& A : Abilities)
		{
			States.AppendTags(A.Owned);
			States.AppendTags(A.Blocked);
			States.AppendTags(A.Required);
		}

		for (const FVBStateRule& Rule : Table.StateRules)
		{
			States.AddTag(Rule.State);
		}

		TArray<FGameplayTag> StateList;
		States.GetGameplayTagArray(StateList);
		StateList.Sort([](const FGameplayTag& A, const FGameplayTag& B) { return A.ToString() < B.ToString(); });

		TArray<FString> Lines;
		for (const FAbilityTags& A : Abilities)
		{
			Lines.Add(FString::Printf(TEXT("identity %s = %s"), *A.Name, *A.Identity.ToStringSimple()));
			Lines.Add(FString::Printf(TEXT("owns %s = %s"), *A.Name, *A.Owned.ToStringSimple()));
			for (const FGameplayTag& Required : A.Required)
			{
				Lines.Add(FString::Printf(TEXT("start %s requires %s"), *A.Name, *Required.ToString()));
			}

			for (const FGameplayTag& State : StateList)
			{
				if (FGameplayTagContainer(State).HasAny(A.Blocked))
				{
					Lines.Add(FString::Printf(TEXT("start %s blocked while %s"), *A.Name, *State.ToString()));
				}

				FGameplayTagContainer CancelledOnEntry;
				Table.AppendCancelsOnEntry(State, CancelledOnEntry);
				if (A.Identity.HasAny(CancelledOnEntry))
				{
					Lines.Add(FString::Printf(TEXT("enter %s cancels %s"), *State.ToString(), *A.Name));
				}
			}

			for (const FAbilityTags& Running : Abilities)
			{
				const bool bByAbility = A.Identity.HasAny(Running.BlocksWhileRunning);
				const bool bByState = Running.Owned.HasAny(A.Blocked);
				if (bByAbility || bByState)
				{
					Lines.Add(FString::Printf(TEXT("start %s blocked while running %s"), *A.Name, *Running.Name));
				}

				if (A.Identity.HasAny(Running.CancelsOnStart))
				{
					Lines.Add(FString::Printf(TEXT("start %s cancels %s"), *Running.Name, *A.Name));
				}
			}
		}

		Lines.Sort();
		return FString::Join(Lines, TEXT("\n")) + TEXT("\n");
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesMatrixTest, "VB.Unit.CombatRules.Matrix",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesMatrixTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatRulesTest;
	const UVBCombatRuleTable* Table = ShippedTable(*this);
	if (!Table)
	{
		return false;
	}

	const TArray<FAbilityTags> Abilities = GatherAbilities(*this, *Table);
	if (!TestTrue(TEXT("abilities found"), Abilities.Num() > 0))
	{
		return false;
	}

	CompareGolden(*this, GoldenRelativePath, BuildMatrix(Abilities, *Table), TEXT("combat rule matrix"));
	return true;
}

// 무엇이 무엇을 막고 취소하는지의 집은 표 하나 - 관계 container 를 채운 ability 클래스는 표가 못 보는 둘째 집이다.
//   반대 대조: 생성자에 ActivationBlockedTags 한 줄을 되돌리면 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesNoRelationsInGAsTest, "VB.Unit.CombatRules.NoRelationsInGAs",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesNoRelationsInGAsTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatRulesTest;
	TArray<const UClass*> Classes = GatherAbilityClasses(*this);
	if (!TestTrue(TEXT("abilities found"), Classes.Num() > 0))
	{
		return false;
	}
	// 줄 수 있는 클래스는 모두 신원을 가진다 - 빈 신원은 State.Dead 줄조차 안 맞는다.
	for (const UClass* Class : Classes)
	{
		TestFalse(FString::Printf(TEXT("%s has an identity (asset tags) the table can match"), *Class->GetName()),
			Class->GetDefaultObject<UGameplayAbility>()->GetAssetTags().IsEmpty());
	}
	// abstract native 기반도 본다(UVBGA_Crouch 는 BP 로만 준다) - 거기 관계는 모든 자식에 닿는다.
	for (TObjectIterator<UClass> It; It; ++It)
	{
		if (It->IsChildOf(UGameplayAbility::StaticClass()) && It->HasAnyClassFlags(CLASS_Abstract)
			&& It->GetPackage()->GetName() == TEXT("/Script/VB"))
		{
			Classes.AddUnique(*It);
		}
	}

	for (const UClass* Class : Classes)
	{
		for (const TCHAR* Container : RelationContainers)
		{
			const FGameplayTagContainer Tags = ReadContainer(*Class->GetDefaultObject<UGameplayAbility>(), Container);
			TestTrue(FString::Printf(TEXT("%s %s is empty (found %s) - move it to DA_CombatRules"), *Class->GetName(), Container,
				*Tags.ToStringSimple()), Tags.IsEmpty());
		}
	}

	return true;
}

// 디자이너가 고치는 것은 애셋이고, 그 텍스트 거울이 모든 수정을 검토 가능한 git diff 로 만든다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesTextMirrorTest, "VB.Unit.CombatRules.TextMirror",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesTextMirrorTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatRulesTest;
	const UVBCombatRuleTable* Table = ShippedTable(*this);
	if (!Table)
	{
		return false;
	}

	CompareGolden(*this, TEXT("Source/VBTests/Golden/DA_CombatRules.txt"), ExportTable(*Table),
		TEXT("combat rule table text mirror"));
	return true;
}

// 엔진은 native gameplay tag 를 Runtime 모듈에만 허락한다 - UncookedOnly 인 이 모듈에 두면 커맨드렛마다 ensure 가 나고 종료 코드가 1 이 된다.
//   시험 태그는 Config/Tags/VBTests.ini 에 두고 VBTestIniTag.h 로 찾는다. 코드 줄 앞의 define 매크로만 센다(주석은 제외).
//   반대 대조: 줄 앞에 그 매크로가 있는 헤더가 있으면 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBNoNativeTagsInTestModuleTest, "VB.Unit.Tags.NoNativeTagsInTestModule",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBNoNativeTagsInTestModuleTest::RunTest(const FString& Parameters)
{
	const FString Needle = FString(TEXT("UE_DEFINE_")) + TEXT("GAMEPLAY_TAG");
	const FString Root = FPaths::Combine(FPaths::ProjectDir(), TEXT("Source/VBTests"));
	TArray<FString> Files;
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.cpp"), /*Files=*/true, /*Directories=*/false);
	IFileManager::Get().FindFilesRecursive(Files, *Root, TEXT("*.h"), /*Files=*/true, /*Directories=*/false, /*bClearFileNames=*/false);
	if (!TestTrue(TEXT("test module sources found"), Files.Num() > 0))
	{
		return false;
	}

	for (const FString& File : Files)
	{
		TArray<FString> Lines;
		FFileHelper::LoadFileToStringArray(Lines, *File);
		for (int32 Index = 0; Index < Lines.Num(); ++Index)
		{
			if (Lines[Index].TrimStart().StartsWith(Needle))
			{
				AddError(FString::Printf(TEXT("%s:%d defines a native gameplay tag in the UncookedOnly test module - register it in Config/Tags/VBTests.ini (VBTestIniTag.h)"),
					*File, Index + 1));
			}
		}
	}

	TestTrue(TEXT("the test doubles' identity resolves"), FGameplayTag(VBTestAbilityTags::AnyAbility).IsValid());
	return true;
}

#if WITH_EDITOR
// 출하 표는 자기 저장 시 검사(UVBCombatRuleTable::IsDataValid)를 통과한다: 줄 중복 없음, 아무 클래스도 안 가진 ability 칸 없음,
//   자기를 막는 줄 없음, 필요하면서 막는 상태 없음, 모든 줄에 Why.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesTableValidTest, "VB.Unit.CombatRules.TableValid",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesTableValidTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatRulesTest;
	const UVBCombatRuleTable* Table = ShippedTable(*this);
	if (!Table)
	{
		return false;
	}

	FDataValidationContext Context;
	const EDataValidationResult Result = Table->IsDataValid(Context);
	TArray<FText> Warnings;
	TArray<FText> Errors;
	Context.SplitIssues(Warnings, Errors);
	for (const FText& Error : Errors)
	{
		AddError(Error.ToString());
	}

	TestTrue(TEXT("shipped table is valid"), Result != EDataValidationResult::Invalid);

	// 반대 대조: 아무 클래스도 안 가진 ability 줄을 넣은 사본은 거절돼야 한다(검사가 그것을 본다는 증명).
	UVBCombatRuleTable* Broken = DuplicateObject(Table, GetTransientPackage());
	FVBAbilityRule& Row = Broken->AbilityRules.AddDefaulted_GetRef();
	Row.Ability = VBGameplayTags::Ability_Medical_Diagnose;
	Row.Why = TEXT("negative control");
	FDataValidationContext BrokenContext;
	TestEqual(TEXT("a row for an ability no class carries is refused"), Broken->IsDataValid(BrokenContext), EDataValidationResult::Invalid);
	return true;
}
#endif

// Traversal(hang, ladder, climb, move, exit): 입력으로 시작하는 ability 는 모두 막고 진입 때 취소한다 - sprint 만 예외(Montage 없이 속도만 고른다).
//   피격 반응은 입력 시작이 아니라 허용된다.
//   반대 대조: traversal 줄 하나에서 Ability.Attack 을 빼면 그 상태의 공격 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesTraversalStatesTest, "VB.Unit.CombatRules.TraversalStates",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesTraversalStatesTest::RunTest(const FString& Parameters)
{
	using namespace VBCombatRulesTest;
	const UVBCombatRuleTable* Table = ShippedTable(*this);
	if (!Table)
	{
		return false;
	}

	const FGameplayTag States[] = { VBGameplayTags::State_Traversal_Hang, VBGameplayTags::State_Traversal_Ladder,
		VBGameplayTags::State_Traversal_Climb, VBGameplayTags::State_Traversal_Move, VBGameplayTags::State_Traversal_Exit };
	const FGameplayTag Blocked[] = { VBGameplayTags::Ability_Attack_Light, VBGameplayTags::Ability_Attack_Riposte,
		VBGameplayTags::Ability_Attack_CinematicSkill, VBGameplayTags::Ability_Defense_Dodge, VBGameplayTags::Ability_Defense_Guard,
		VBGameplayTags::Ability_Medical_Execute, VBGameplayTags::Ability_Medical_Purify, VBGameplayTags::Ability_Interact,
		VBGameplayTags::Ability_Item_Use, VBGameplayTags::Ability_Locomotion_Crouch, VBGameplayTags::Ability_Weapon_Summon };
	const FGameplayTag Allowed[] = { VBGameplayTags::Ability_Locomotion_Sprint, VBGameplayTags::Ability_Reaction_HitReact,
		VBGameplayTags::Ability_Reaction_Stagger };
	for (const FGameplayTag& State : States)
	{
		FGameplayTagContainer Cancelled;
		Table->AppendCancelsOnEntry(State, Cancelled);
		for (const FGameplayTag& Ability : Blocked)
		{
			FGameplayTagContainer BlockedBy;
			FGameplayTagContainer Required;
			Table->AppendActivationTags(FGameplayTagContainer(Ability), BlockedBy, Required);
			TestTrue(FString::Printf(TEXT("%s blocks %s"), *State.ToString(), *Ability.ToString()), BlockedBy.HasTagExact(State));
			TestTrue(FString::Printf(TEXT("%s cancels %s on entry"), *State.ToString(), *Ability.ToString()),
				FGameplayTagContainer(Ability).HasAny(Cancelled));
		}

		for (const FGameplayTag& Ability : Allowed)
		{
			FGameplayTagContainer BlockedBy;
			FGameplayTagContainer Required;
			Table->AppendActivationTags(FGameplayTagContainer(Ability), BlockedBy, Required);
			TestFalse(FString::Printf(TEXT("%s allows %s"), *State.ToString(), *Ability.ToString()), BlockedBy.HasTagExact(State));
			TestFalse(FString::Printf(TEXT("%s keeps %s running"), *State.ToString(), *Ability.ToString()),
				FGameplayTagContainer(Ability).HasAny(Cancelled));
		}
	}

	return true;
}

// 상태가 authority 에 생기는 순간 실행 중 ability 를 취소하고(UVBAbilitySystemComponent::OnTagUpdated), 그동안 시작을 막고, 풀리면 다시 허락한다.
//   ragdoll 줄을 쓴다(거기선 interact 가 막히고 취소된다).
//   반대 대조: OnTagUpdated override 를 빼면 ragdoll entry cancels the running interact 줄이 실패한다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBCombatRulesCancelOnEntryTest, "VB.Unit.CombatRules.CancelOnEntry",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBCombatRulesCancelOnEntryTest::RunTest(const FString& Parameters)
{
	FVBIsolatedGameWorld Isolated;
	if (!Isolated.Init(*this))
	{
		return false;
	}

	AActor* Avatar = Isolated.World->SpawnActor<AActor>();
	if (!TestNotNull(TEXT("avatar"), Avatar))
	{
		return false;
	}

	UVBAbilitySystemComponent* ASC = NewObject<UVBAbilitySystemComponent>(Avatar);
	ASC->RegisterComponent();
	ASC->InitAbilityActorInfo(Avatar, Avatar);
	const FGameplayAbilitySpecHandle Interact = ASC->GiveAbility(FGameplayAbilitySpec(UVBCinematicSkillTestInteract::StaticClass()));
	auto IsActive = [ASC, Interact]
	{
		const FGameplayAbilitySpec* Spec = ASC->FindAbilitySpecFromHandle(Interact);
		return Spec && Spec->IsActive();
	};

	if (!TestTrue(TEXT("interact starts"), ASC->TryActivateAbility(Interact)))
	{
		return false;
	}

	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 1);
	TestFalse(TEXT("ragdoll entry cancels the running interact"), IsActive());
	TestFalse(TEXT("interact does not start during ragdoll"), ASC->TryActivateAbility(Interact));
	ASC->SetLooseGameplayTagCount(VBGameplayTags::State_Combat_Ragdoll, 0);
	TestTrue(TEXT("interact starts again after ragdoll"), ASC->TryActivateAbility(Interact));
	ASC->CancelAbilityHandle(Interact);
	return true;
}
#endif
