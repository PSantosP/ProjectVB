// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBGA_Guard.h"
#include "AbilitySystem/Abilities/VBGA_MeleeAttackBase.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBAttackConfig.h"
#include "Data/VBWeaponStateConfig.h"
#include "Inventory/Enhancement/VBEnhancementComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "VBPlayerBodyTestFixture.h"
#include "VBWeaponDefinitionTestUtil.h"
#include "VBWeaponSetTestTypes.h"

// 손에 든 무기가 자기 ability set 을 준다 - 꺼내면 정의를 SourceObject 로 주고, 넣거나 바꾸거나 avatar session 을 떠나면 거둔다.
//   넣은 채 누르면 먼저 꺼낸다.
namespace VBWeaponSetTest
{
	int32 CountFrom(const UAbilitySystemComponent& ASC, const UObject* Source)
	{
		int32 Count = 0;
		for (const FGameplayAbilitySpec& Spec : ASC.GetActivatableAbilities())
		{
			Count += Spec.SourceObject.Get() == Source ? 1 : 0;
		}

		return Count;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponSetTest, "VB.Unit.Weapon.Set",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponSetTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponSetTest;
	using namespace VBWeaponDefinitionTest;
	TestTrue(TEXT("guard rejects client activation requests - its only door is the server press"),
		GetDefault<UVBGA_Guard>()->GetNetSecurityPolicy() == EGameplayAbilityNetSecurityPolicy::ServerOnlyExecution);

	VBPlayerBodyTest::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.A);
	UAbilitySystemComponent& ASC = *F.ASC;
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(F.A);
	if (!TestNotNull(TEXT("body weapon state"), Weapons))
	{
		return false;
	}

	UVBWeaponDefinition* Blade = Make(Weapons, EVBWeaponType::Blade);
	UVBWeaponDefinition* BigSword = Make(Weapons, EVBWeaponType::BigSword);
	GiveSet(*Blade, {UVBWeaponSetTestAttack::StaticClass()});
	GiveSet(*BigSword, {UVBWeaponSetTestAttack::StaticClass()});
	Equip(*Weapons, {Blade, BigSword}, EVBWeaponType::None);
	Weapons->SetOwnedWeapons({EVBWeaponType::Blade, EVBWeaponType::BigSword});

	// 꺼내기, 넣기, 바꾸기.
	Weapons->SummonWeapon(EVBWeaponType::Blade);
	TestEqual(TEXT("draw: the katana's set is given, SourceObject = its definition"), CountFrom(ASC, Blade), 1);
	Weapons->SheatheWeapon();
	TestEqual(TEXT("sheathe: taken"), CountFrom(ASC, Blade), 0);
	Weapons->SummonWeapon(EVBWeaponType::Blade);
	Weapons->SummonWeapon(EVBWeaponType::BigSword);
	TestEqual(TEXT("swap: the katana's set is taken"), CountFrom(ASC, Blade), 0);
	TestEqual(TEXT("swap: the big sword's set is given"), CountFrom(ASC, BigSword), 1);

	// 다른 몸을 possess 하면 PlayerState ASC 에 이 무기의 것이 남지 않고, 돌아오면 다시 준다.
	F.PC->Possess(F.B);
	TestEqual(TEXT("avatar session ended: taken"), CountFrom(ASC, BigSword), 0);
	F.PC->Possess(F.A);
	TestEqual(TEXT("avatar session again with the weapon in hand: given again"), CountFrom(ASC, BigSword), 1);

	// 넣은 채 누르면, 그 입력의 무기 능력이 지금 시작될 수 있을 때만 먼저 꺼낸다.
	Weapons->SheatheWeapon();
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Dead);
	TestTrue(TEXT("press while the attack cannot start: not drawn"),
		Weapons->DrawForPress(VBGameplayTags::Input_Attack_Light) == EVBPressDraw::NotNow);
	TestFalse(TEXT("press while the attack cannot start: still sheathed"), Weapons->IsWeaponSummoned());
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	TestTrue(TEXT("press with no weapon ability for that input: not drawn"),
		Weapons->DrawForPress(VBGameplayTags::Input_Combat_Guard) == EVBPressDraw::NotNow);
	TestTrue(TEXT("sheathed attack press: drawn"), Weapons->DrawForPress(VBGameplayTags::Input_Attack_Light) == EVBPressDraw::Drawn);
	TestEqual(TEXT("sheathed attack press: the selected weapon's set is there for the same press"), CountFrom(ASC, BigSword), 1);
	TestTrue(TEXT("press with the weapon in hand"), Weapons->DrawForPress(VBGameplayTags::Input_Attack_Light) == EVBPressDraw::InHand);
	Weapons->SheatheWeapon();
	Weapons->SetOwnedWeapons({});
	Equip(*Weapons, {Blade, BigSword}, EVBWeaponType::None);
	TestTrue(TEXT("nothing owned: no weapon to draw"),
		Weapons->DrawForPress(VBGameplayTags::Input_Attack_Light) == EVBPressDraw::NoOwnedWeapon);

	// 공격 leaf 는 선택된 무기가 아니라 그 능력을 준 무기에서 온다.
	UClass* LightAttack = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
		TEXT("/Game/VB/GAS/Abilities/BP_GA_LightAttack.BP_GA_LightAttack_C"));
	if (TestNotNull(TEXT("shipped BP_GA_LightAttack"), LightAttack))
	{
		UVBAttackConfig* BladeLeaf = NewObject<UVBAttackConfig>(Blade);
		SetField(*Blade, TEXT("LightAttack"), TObjectPtr<UVBAttackConfig>(BladeLeaf));
		SetField(*BigSword, TEXT("LightAttack"), TObjectPtr<UVBAttackConfig>(NewObject<UVBAttackConfig>(BigSword)));
		Equip(*Weapons, {Blade, BigSword}, EVBWeaponType::BigSword);
		const FGameplayAbilitySpecHandle Handle = ASC.GiveAbility(FGameplayAbilitySpec(LightAttack, 1, INDEX_NONE, Blade));
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		const UVBGA_MeleeAttackBase* Instance = Spec ? Cast<UVBGA_MeleeAttackBase>(Spec->GetPrimaryInstance()) : nullptr;
		if (TestNotNull(TEXT("attack instance"), Instance))
		{
			TestTrue(TEXT("leaf = the giving weapon's (katana) while the big sword is selected"),
				Instance->PeekAttackConfig() == BladeLeaf);
		}

		ASC.ClearAbility(Handle);
	}

	return true;
}
// 손에 든 무기가 강화 배율을 WeaponDamageScale 에 둔다 - 무기와 강화 단계를 따르고, 맨손이면 1 이다.
//   들고 있는 동안 단계가 바뀌면 효과만 다시 걸고 ability spec 은 그대로 둔다.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBWeaponStatsTest, "VB.Unit.Weapon.Stats",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBWeaponStatsTest::RunTest(const FString& Parameters)
{
	using namespace VBWeaponSetTest;
	using namespace VBWeaponDefinitionTest;
	VBPlayerBodyTest::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBEnhancementComponent* Enhancement = F.State->FindComponentByClass<UVBEnhancementComponent>();
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(F.A);
	if (!TestNotNull(TEXT("enhancement on the player state"), Enhancement) || !TestNotNull(TEXT("body weapon state"), Weapons))
	{
		return false;
	}

	if (!TestTrue(TEXT("the shipped enhancement config allows stages"), Enhancement->GetMaxLevel() >= 3))
	{
		return false;
	}

	UVBWeaponStateConfig* Config = NewObject<UVBWeaponStateConfig>(Weapons);
	Config->WeaponEnhancementEffect = UVBWeaponSetTestEnhancementEffect::StaticClass();
	SetField(*Weapons, TEXT("WeaponStateConfig"), TObjectPtr<UVBWeaponStateConfig>(Config));
	UVBWeaponDefinition* Blade = Make(Weapons, EVBWeaponType::Blade);
	UVBWeaponDefinition* BigSword = Make(Weapons, EVBWeaponType::BigSword);
	GiveSet(*Blade, {UVBWeaponSetTestAttack::StaticClass()});
	GiveSet(*BigSword, {UVBWeaponSetTestAttack::StaticClass()});
	Equip(*Weapons, {Blade, BigSword}, EVBWeaponType::None);
	Weapons->SetOwnedWeapons({EVBWeaponType::Blade, EVBWeaponType::BigSword});
	F.PC->Possess(F.A);
	UAbilitySystemComponent& ASC = *F.ASC;
	const auto Scale = [&ASC]() { return ASC.GetNumericAttribute(UVBCombatAttributeSet::GetWeaponDamageScaleAttribute()); };

	TestTrue(TEXT("katana +3"), Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade), 3));
	TestEqual(TEXT("no weapon in hand: 1"), Scale(), 1.0f);
	Weapons->SummonWeapon(EVBWeaponType::Blade);
	TestEqual(TEXT("katana in hand: its +3 scale"), Scale(), Enhancement->GetWeaponDamageScale(EVBWeaponType::Blade));
	TestNotEqual(TEXT("and +3 is not the bare 1 (the shipped track scales)"), Scale(), 1.0f);
	Weapons->SheatheWeapon();
	TestEqual(TEXT("sheathed: back to 1"), Scale(), 1.0f);

	Weapons->SummonWeapon(EVBWeaponType::Blade);
	const FGameplayAbilitySpec* Before = ASC.GetActivatableAbilities().FindByPredicate([Blade](const FGameplayAbilitySpec& Spec) { return Spec.SourceObject.Get() == Blade; });
	const FGameplayAbilitySpecHandle SpecBefore = Before ? Before->Handle : FGameplayAbilitySpecHandle();
	TestTrue(TEXT("katana +1 while in hand"), Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::Blade), 1));
	TestEqual(TEXT("the scale follows the stage while in hand"), Scale(), Enhancement->GetWeaponDamageScale(EVBWeaponType::Blade));
	TestTrue(TEXT("the weapon's ability spec was not re-given"), SpecBefore.IsValid()
		&& ASC.FindAbilitySpecFromHandle(SpecBefore) != nullptr);
	TestTrue(TEXT("big sword +2 while the katana is in hand"),
		Enhancement->SetLevel(FVBEnhanceTarget::ForWeapon(EVBWeaponType::BigSword), 2));
	TestEqual(TEXT("another weapon's stage does not touch the one in hand"), Scale(),
		Enhancement->GetWeaponDamageScale(EVBWeaponType::Blade));

	Weapons->SummonWeapon(EVBWeaponType::BigSword);
	TestEqual(TEXT("swap: the big sword's scale"), Scale(), Enhancement->GetWeaponDamageScale(EVBWeaponType::BigSword));
	F.PC->Possess(F.B);
	TestEqual(TEXT("avatar session ended: 1"), Scale(), 1.0f);
	return true;
}
#endif
