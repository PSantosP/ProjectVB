// ProjectVB Copyright (c) 2025. All Rights Reserved.

// UVBWeaponDefinition 이 무기별 데이터의 유일한 집이고, 무기 상태는 무기 종류로 그것을 찾는다

#include "Misc/AutomationTest.h"

#include "AbilitySystem/Abilities/VBGA_AirCombo.h"
#include "AbilitySystem/Abilities/VBGA_Guard.h"
#include "AbilitySystem/Abilities/VBGA_LightAttack.h"
#include "AbilitySystem/VBAbilitySet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBCharacter.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBAirComboConfig.h"
#include "GameplayEffect.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "Data/VBWeaponStateConfig.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "Misc/DataValidation.h"
#include "UObject/StrongObjectPtr.h"
#include "VBWeaponDefinitionTestUtil.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponDefinitionLookupTest, "VB.Unit.Weapon.Definition.Lookup",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponDefinitionLookupTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponDefinitionTest;
	const TStrongObjectPtr<UVBWeaponStateComponent> Weapons(NewObject<UVBWeaponStateComponent>());
	UVBWeaponDefinition* Blade = Make(Weapons.Get(), EVBWeaponType::Blade);
	UVBWeaponDefinition* Fighter = Make(Weapons.Get(), EVBWeaponType::Fighter);
	Equip(*Weapons, {Blade, Fighter}, EVBWeaponType::Fighter);

	TestTrue(TEXT("finds the katana"), Weapons->FindDefinition(EVBWeaponType::Blade) == Blade);
	TestNull(TEXT("bare hands have no definition"), Weapons->FindDefinition(EVBWeaponType::None));
	TestNull(TEXT("an unknown weapon has no definition"), Weapons->FindDefinition(EVBWeaponType::BigSword));
	TestTrue(TEXT("the selected weapon's definition"), Weapons->GetSelectedDefinition() == Fighter);
	TestNull(TEXT("no dodge set unless the weapon has one"), Blade->FindDodgeSet());

	Equip(*Weapons, {Blade, Fighter}, EVBWeaponType::None);
	TestNull(TEXT("nothing selected"), Weapons->GetSelectedDefinition());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponDefinitionValidationTest, "VB.Unit.Weapon.Definition.Validation",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponDefinitionValidationTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponDefinitionTest;
	const TStrongObjectPtr<UVBWeaponDefinition> Blade(Make(GetTransientPackage(), EVBWeaponType::Blade));
	TestEqual(TEXT("a weapon without an ability set gives nothing in the hand"), Blade->FindProblems().Num(), 1);

	GiveSet(*Blade, {UVBGA_LightAttack::StaticClass(), UVBGA_Guard::StaticClass()});
	TestEqual(TEXT("the set gives a light attack but the weapon has no light attack leaf"), Blade->FindProblems().Num(), 1);
	SetField(*Blade, TEXT("LightAttack"), TObjectPtr<UVBAttackConfig>(NewObject<UVBAttackConfig>(Blade.Get())));
	TestEqual(TEXT("leaf present, guard without montage content is fine"), Blade->FindProblems().Num(), 0);

	const TStrongObjectPtr<UVBWeaponDefinition> BareHands(Make(GetTransientPackage(), EVBWeaponType::None));
	GiveSet(*BareHands, {});
	TestEqual(TEXT("bare hands are not a weapon"), BareHands->FindProblems().Num(), 1);

	SetField(*Blade, TEXT("DisplayName"), FText::GetEmpty());
	TestEqual(TEXT("a weapon the wheel cannot name"), Blade->FindProblems().Num(), 1);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponAirGrantContractTest, "VB.Unit.Weapon.Definition.AirGrantContract",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponAirGrantContractTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponDefinitionTest;
	const TStrongObjectPtr<UVBWeaponDefinition> Blade(Make(GetTransientPackage(), EVBWeaponType::Blade));
	GiveSet(*Blade, {});
	TestEqual(TEXT("a weapon without air combat needs no air grant"), Blade->FindProblems().Num(), 0);
	UVBAirComboConfig* Air = NewObject<UVBAirComboConfig>(Blade.Get());
	Air->StringAttack = NewObject<UVBAttackConfig>(Air);
	SetField(*Blade, TEXT("AirCombo"), TObjectPtr<UVBAirComboConfig>(Air));
	TestEqual(TEXT("authored air combat without its grant is rejected"), Blade->FindProblems().Num(), 1);
	GiveSet(*Blade, {UVBGA_AirCombo::StaticClass()});
	TestEqual(TEXT("grant and authored air attack agree"), Blade->FindProblems().Num(), 0);
	SetField(*Blade, TEXT("AirCombo"), TObjectPtr<UVBAirComboConfig>());
	TestEqual(TEXT("air grant without its weapon leaf is rejected"), Blade->FindProblems().Num(), 1);
	return true;
}

// 숫자 키 -> 가진 무기 중 InputSlot 이 그 칸인 것. 한 칸을 나누는 무기(프롤로그 한손검 -> 카타나)는 함께 가지지 않는다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponDefinitionSlotTest, "VB.Unit.Weapon.Definition.Slot",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponDefinitionSlotTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponDefinitionTest;
	const TStrongObjectPtr<UVBWeaponStateComponent> Weapons(NewObject<UVBWeaponStateComponent>());
	UVBWeaponDefinition* Blade = Make(Weapons.Get(), EVBWeaponType::Blade);
	UVBWeaponDefinition* Sword = Make(Weapons.Get(), EVBWeaponType::OneHandSword);
	UVBWeaponDefinition* Fighter = Make(Weapons.Get(), EVBWeaponType::Fighter);
	SetField(*Blade, TEXT("InputSlot"), 1);
	SetField(*Sword, TEXT("InputSlot"), 1);
	SetField(*Fighter, TEXT("InputSlot"), 3);
	Equip(*Weapons, {Blade, Sword, Fighter}, EVBWeaponType::None);
	auto Own = [&Weapons](const TArray<EVBWeaponType>& Owned) { SetField(*Weapons, TEXT("OwnedWeapons"), Owned); };

	Own({EVBWeaponType::OneHandSword, EVBWeaponType::Fighter});
	TestTrue(TEXT("prologue: key 1 draws the owned sword, not the unowned katana listed first"),
		Weapons->FindDefinitionForSlot(1) == Sword);
	TestTrue(TEXT("key 3"), Weapons->FindDefinitionForSlot(3) == Fighter);
	TestNull(TEXT("slot 0 is never a key"), Weapons->FindDefinitionForSlot(0));
	TestNull(TEXT("an empty key"), Weapons->FindDefinitionForSlot(2));

	Own({EVBWeaponType::Blade, EVBWeaponType::Fighter});
	TestTrue(TEXT("after the swap key 1 draws the katana"), Weapons->FindDefinitionForSlot(1) == Blade);

	Own({EVBWeaponType::Blade, EVBWeaponType::OneHandSword});
	AddExpectedMessage(TEXT("숫자 키 1"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	TestTrue(TEXT("both owned: the first, with a warning"), Weapons->FindDefinitionForSlot(1) == Blade);
	return true;
}

// 출시 플레이어 몸은 무기마다 검사를 통과한 정의 하나로 공격, 반격을 이름 짓고 자기 ability set 으로 준다
// 플레이어 시작 set 은 공격, 반격, 가드를 주지 않는다
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponDefinitionShippedTest, "VB.Unit.Weapon.Definition.Shipped",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponDefinitionShippedTest::RunTest(const FString& Parameters)
{
	UClass* CharacterClass = LoadClass<AVBCharacter>(nullptr, TEXT("/Game/VB/Blueprints/Characters/BP_VBCharacter.BP_VBCharacter_C"));
	if (!TestNotNull(TEXT("BP_VBCharacter"), CharacterClass))
	{
		return false;
	}

	const UVBWeaponStateComponent* Weapons = GetDefault<AVBCharacter>(CharacterClass)->FindComponentByClass<UVBWeaponStateComponent>();
	if (!TestNotNull(TEXT("weapon state on the body"), Weapons))
	{
		return false;
	}

	const FArrayProperty* ListProperty = FindFProperty<FArrayProperty>(UVBWeaponStateComponent::StaticClass(), TEXT("WeaponDefinitions"));
	if (!TestNotNull(TEXT("WeaponDefinitions property"), ListProperty))
	{
		return false;
	}

	const TArray<TObjectPtr<const UVBWeaponDefinition>>& List =
		*ListProperty->ContainerPtrToValuePtr<TArray<TObjectPtr<const UVBWeaponDefinition>>>(Weapons);

	TSet<EVBWeaponType> Seen;
	for (const UVBWeaponDefinition* Definition : List)
	{
		if (!TestNotNull(TEXT("no empty entry"), Definition))
		{
			continue;
		}

		const FString Name = Definition->GetName();
		bool bAlreadySeen = false;
		Seen.Add(Definition->GetWeaponType(), &bAlreadySeen);
		TestFalse(*FString::Printf(TEXT("%s: one definition per weapon"), *Name), bAlreadySeen);
		TestTrue(*FString::Printf(TEXT("%s: the body finds it by its type"), *Name),
			Weapons->FindDefinition(Definition->GetWeaponType()) == Definition);
		TestNotNull(*FString::Printf(TEXT("%s: light attack leaf"), *Name), Definition->GetLightAttack());
		TestNotNull(*FString::Printf(TEXT("%s: riposte leaf"), *Name), Definition->GetRiposte());
		bool bArmedTag = false;
		bool bAttackPower = false;
		if (TestNotNull(*FString::Printf(TEXT("%s: ability set"), *Name), Definition->GetAbilitySet()))
		{
			for (const FVBAbilitySetEffect& Entry : Definition->GetAbilitySet()->GetGrantedEffects())
			{
				bArmedTag |= Entry.Effect && Entry.Effect->GetDefaultObject<UGameplayEffect>()->GetGrantedTags().HasTagExact(VBGameplayTags::State_Armed);
				bAttackPower |= Entry.Effect && Entry.Effect->GetDefaultObject<UGameplayEffect>()->Modifiers.ContainsByPredicate(
					[](const FGameplayModifierInfo& Modifier) { return Modifier.Attribute == UVBCombatAttributeSet::GetAttackPowerAttribute(); });
			}
		}

		TestTrue(*FString::Printf(TEXT("%s: the weapon effect grants State.Armed while the weapon is in hand"), *Name), bArmedTag);
		TestTrue(*FString::Printf(TEXT("%s: the weapon effect adds the weapon's base AttackPower"), *Name), bAttackPower);
#if WITH_EDITOR
		FDataValidationContext Context;
		TestTrue(*FString::Printf(TEXT("%s: passes validation"), *Name),
			Definition->IsDataValid(Context) != EDataValidationResult::Invalid);
#endif
	}

	for (const EVBWeaponType Expected : {EVBWeaponType::Blade, EVBWeaponType::BigSword, EVBWeaponType::Fighter})
	{
		TestTrue(*FString::Printf(TEXT("the body knows %s"), *UEnum::GetValueAsString(Expected)), Seen.Contains(Expected));
	}
	// 숫자 키는 정의가 정한다. 종전 배치(1 카타나, 2 대검, 3 격투)가 그대로다 - 플레이 봇은 3 을 격투로 누른다.
	const TPair<EVBWeaponType, int32> Keys[] = {{EVBWeaponType::Blade, 1}, {EVBWeaponType::BigSword, 2}, {EVBWeaponType::Fighter, 3}};
	for (const TPair<EVBWeaponType, int32>& Key : Keys)
	{
		const UVBWeaponDefinition* Definition = Weapons->FindDefinition(Key.Key);
		TestTrue(*FString::Printf(TEXT("%s is on key %d"), *UEnum::GetValueAsString(Key.Key), Key.Value), Definition
			&& Definition->GetInputSlot() == Key.Value);
	}

	// 손에 든 무기의 강화 효과: 무기 상태 설정이 이름을 대고, 그 효과는 WeaponDamageScale 을 SetByCaller 로 쓴다(값은 강화 컴포넌트).
	const FObjectProperty* ConfigProperty = FindFProperty<FObjectProperty>(UVBWeaponStateComponent::StaticClass(),
		TEXT("WeaponStateConfig"));
	const UVBWeaponStateConfig* StateConfig = ConfigProperty ? Cast<UVBWeaponStateConfig>(ConfigProperty->GetObjectPropertyValue_InContainer(Weapons)) : nullptr;
	const UGameplayEffect* Enhancement = StateConfig && StateConfig->WeaponEnhancementEffect ? StateConfig->WeaponEnhancementEffect->GetDefaultObject<UGameplayEffect>() : nullptr;
	if (TestNotNull(TEXT("the body's weapon state config names the weapon enhancement effect"), Enhancement))
	{
		TestTrue(TEXT("the enhancement effect writes WeaponDamageScale by Data.WeaponDamageScale"),
			Enhancement->Modifiers.ContainsByPredicate([](const FGameplayModifierInfo& Modifier)
		{
			return Modifier.Attribute == UVBCombatAttributeSet::GetWeaponDamageScaleAttribute()
				&& Modifier.ModifierMagnitude.GetMagnitudeCalculationType() == EGameplayEffectMagnitudeCalculation::SetByCaller
				&& Modifier.ModifierMagnitude.GetSetByCallerFloat().DataTag == VBGameplayTags::Data_WeaponDamageScale;
		}));
	}

	const UVBAbilitySet* StartSet = LoadObject<UVBAbilitySet>(nullptr,
		TEXT("/Game/VB/GAS/AbilitySets/DA_AbilitySet_Player.DA_AbilitySet_Player"));
	if (!TestNotNull(TEXT("player start set"), StartSet))
	{
		return false;
	}

	for (const FVBAbilitySetAbility& Entry : StartSet->GetGrantedAbilities())
	{
		const UVBGameplayAbility* Ability = Entry.Ability ? Cast<UVBGameplayAbility>(Entry.Ability->GetDefaultObject()) : nullptr;
		const bool bWeaponInput = Ability && (Ability->InputTag.MatchesTagExact(VBGameplayTags::Input_Attack_Light)
			|| Ability->InputTag.MatchesTagExact(VBGameplayTags::Input_Combat_Guard));
		TestFalse(*FString::Printf(TEXT("start set: %s comes from the weapon in hand, not the start set"),
			*GetNameSafe(Entry.Ability)), bWeaponInput);
	}

	return true;
}
