// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "AbilitySystem/Abilities/VBGA_Guard.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBGuardComponent.h"
#include "Character/VBWeaponStateComponent.h"
#include "VBPlayerBodyTestFixture.h"
#include "VBWeaponDefinitionTestUtil.h"

// 막기 상태는 캐릭터의 guard component 가 가진다: 손에 무기 정의가 있을 때 한 번만 시작(패링 시계 유지), 반격 창은 더한 ASC 에서
//   자기 몫만 세고, 막기를 끝내도 창은 남고, ability system 을 떠나면 죽었든 아니든 모두 지운다. Montage 재생은 anim BP 가 없어 안 본다.
//   반대 대조: BeginGuard 의 이미 막는 중 return 빼기, 막기가 안 시작됐을 때 guard ability 의 EndAbility 빼기.
namespace VBGuardSessionTest
{
	constexpr EAutomationTestFlags Flags = EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter;

	int32 WindowCount(const UAbilitySystemComponent& ASC) { return ASC.GetTagCount(VBGameplayTags::State_Combat_RiposteWindow); }

	struct FGuardFixture
	{
		VBPlayerBodyTest::FFixture Body;
		UVBGuardComponent* GuardA = nullptr;
		UVBGuardComponent* GuardB = nullptr;
		UVBWeaponStateComponent* Weapons = nullptr;
		UVBWeaponDefinition* Blade = nullptr;

		bool Initialize(FAutomationTestBase& Test)
		{
			if (!Body.Initialize(Test))
			{
				return false;
			}

			Body.PC->Possess(Body.A);
			GuardA = UVBGuardComponent::FindFor(Body.A);
			GuardB = UVBGuardComponent::FindFor(Body.B);
			Weapons = UVBWeaponStateComponent::FindFor(Body.A);
			if (!Test.TestNotNull(TEXT("body guard component"), GuardA) || !Test.TestNotNull(TEXT("second body guard component"), GuardB)
				|| !Test.TestNotNull(TEXT("body weapon state"), Weapons))
			{
				return false;
			}

			Blade = VBWeaponDefinitionTest::Make(Weapons, EVBWeaponType::Blade);
			return true;
		}

		void Hold(EVBWeaponType Selected) { VBWeaponDefinitionTest::Equip(*Weapons, {Blade}, Selected); }
	};
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardSessionEntryTest, "VB.Unit.Character.GuardSession.Entry", VBGuardSessionTest::Flags)
bool FVBGuardSessionEntryTest::RunTest(const FString& Parameters)
{
	VBGuardSessionTest::FGuardFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UVBGuardComponent& Guard = *F.GuardA;

	F.Hold(EVBWeaponType::None);
	TestFalse(TEXT("no weapon definition in hand: no guard"), Guard.BeginGuard());
	TestFalse(TEXT("no weapon definition in hand: not guarding"), Guard.IsGuarding());
	TestEqual(TEXT("not guarding: elapsed is outside any parry window"), Guard.GetGuardElapsed(), TNumericLimits<float>::Max());

	F.Hold(EVBWeaponType::Blade);
	TestTrue(TEXT("guard starts with a weapon definition in hand"), Guard.BeginGuard());
	// 패링 창은 첫 시작부터 잰다 - 다시 시작(ability 재활성)해도 시계를 다시 돌리면 안 된다.
	F.Body.World->TimeSeconds += 0.5f;
	TestTrue(TEXT("starting again keeps the guard"), Guard.BeginGuard());
	TestEqual(TEXT("starting again keeps the start time"), Guard.GetGuardElapsed(), 0.5f, 0.001f);
	Guard.EndGuard();
	TestFalse(TEXT("ended: not guarding"), Guard.IsGuarding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardSessionRiposteTest, "VB.Unit.Character.GuardSession.RiposteWindow", VBGuardSessionTest::Flags)
bool FVBGuardSessionRiposteTest::RunTest(const FString& Parameters)
{
	using VBGuardSessionTest::WindowCount;
	VBGuardSessionTest::FGuardFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Body.ASC;
	const int32 Before = WindowCount(ASC);

	F.GuardA->OpenRiposteWindow(1.0f, nullptr);
	TestEqual(TEXT("open: this body's share added"), WindowCount(ASC), Before + 1);
	F.GuardA->OpenRiposteWindow(1.0f, nullptr);
	TestEqual(TEXT("open again on the same ASC: no second share"), WindowCount(ASC), Before + 1);

	F.GuardA->ReleaseSession();
	F.GuardA->ReleaseSession();
	TestEqual(TEXT("releasing twice takes one share"), WindowCount(ASC), Before);

	// 공유 PlayerState ASC 에 다른 몫(앞선 몸의 늦은 창)이 이미 있어도 이 몸은 자기 몫만 옮긴다.
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
	F.GuardA->OpenRiposteWindow(1.0f, nullptr);
	TestEqual(TEXT("with another share present, open still adds this body's share"), WindowCount(ASC), Before + 2);
	F.GuardA->ReleaseSession();
	TestEqual(TEXT("release leaves the other share"), WindowCount(ASC), Before + 1);
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);

	F.GuardA->OpenRiposteWindow(1.0f, nullptr);
	F.GuardA->OpenRiposteWindow(0.0f, nullptr);
	TestEqual(TEXT("length 0 (no riposte owned): the open window closes and none opens"), WindowCount(ASC), Before);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardSessionTeardownTest, "VB.Unit.Character.GuardSession.Teardown", VBGuardSessionTest::Flags)
bool FVBGuardSessionTeardownTest::RunTest(const FString& Parameters)
{
	using VBGuardSessionTest::WindowCount;
	VBGuardSessionTest::FGuardFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Body.ASC;
	const int32 Before = WindowCount(ASC);
	F.Hold(EVBWeaponType::Blade);

	F.GuardA->BeginGuard();
	F.GuardA->OpenRiposteWindow(1.0f, nullptr);
	F.GuardA->EndGuard();
	TestEqual(TEXT("ending the guard keeps the riposte window (it outlives the guard)"), WindowCount(ASC), Before + 1);

	// 죽은 채 ability system 을 떠난다 - 해제는 새 요청이 지나는 문을 거치지 않는다.
	F.GuardA->BeginGuard();
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Dead);
	F.Body.PC->Possess(F.Body.B);
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Dead);
	TestEqual(TEXT("left the ability system: the window share is returned to the ASC it was added to"), WindowCount(ASC), Before);
	TestFalse(TEXT("left the ability system: not guarding"), F.GuardA->IsGuarding());
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardSessionStaminaTest, "VB.Unit.Character.GuardSession.Stamina", VBGuardSessionTest::Flags)
bool FVBGuardSessionStaminaTest::RunTest(const FString& Parameters)
{
	VBGuardSessionTest::FGuardFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Body.ASC;
	const float Max = ASC.GetNumericAttribute(UVBCombatAttributeSet::GetMaxStaminaAttribute());
	ASC.SetNumericAttributeBase(UVBCombatAttributeSet::GetStaminaAttribute(), Max * 0.5f);
	F.GuardA->ApplyGuardStaminaDelta(0.0f);
	TestEqual(TEXT("zero delta is ignored"), ASC.GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute()), Max * 0.5f);
	F.GuardA->ApplyGuardStaminaDelta(Max * 10.0f);
	TestEqual(TEXT("gain clamps at max"), ASC.GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute()), Max);
	F.GuardA->ApplyGuardStaminaDelta(-Max * 10.0f);
	TestEqual(TEXT("cost clamps at 0"), ASC.GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute()), 0.0f);
	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardSessionAbilityTest, "VB.Unit.Character.GuardSession.AbilityWithoutWeapon", VBGuardSessionTest::Flags)
bool FVBGuardSessionAbilityTest::RunTest(const FString& Parameters)
{
	VBGuardSessionTest::FGuardFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	UAbilitySystemComponent& ASC = *F.Body.ASC;
	// UVBGA_Guard 는 abstract - 실제 guard ability 는 출하 blueprint 다.
	UClass* GuardClass = StaticLoadClass(UVBGA_Guard::StaticClass(), nullptr,
		TEXT("/Game/VB/GAS/Abilities/BP_GA_Guard.BP_GA_Guard_C"));
	if (!TestNotNull(TEXT("shipped BP_GA_Guard"), GuardClass))
	{
		return false;
	}

	const FGameplayAbilitySpecHandle Handle = ASC.GiveAbility(FGameplayAbilitySpec(GuardClass, 1, INDEX_NONE, F.Blade));

	// 대조: 손에 katana 가 있으면 실제 guard ability 가 막기를 시작한다.
	F.Hold(EVBWeaponType::Blade);
	ASC.TryActivateAbility(Handle);
	if (!TestTrue(TEXT("control: the guard ability guards with a weapon in hand"), F.GuardA->IsGuarding()))
	{
		return false;
	}

	ASC.CancelAbilityHandle(Handle);

	// spec 은 여전히 무기에서 왔지만 손에 정의가 없다 - ability 가 끝나고 Guarding 태그를 남기지 않는다.
	F.Hold(EVBWeaponType::None);
	ASC.TryActivateAbility(Handle);
	const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
	TestFalse(TEXT("no definition in hand: the ability is not left active"), Spec && Spec->IsActive());
	TestEqual(TEXT("no definition in hand: no Guarding tag left"), ASC.GetTagCount(VBGameplayTags::State_Combat_Guarding), 0);
	TestFalse(TEXT("no definition in hand: not guarding"), F.GuardA->IsGuarding());
	ASC.ClearAbility(Handle);
	return true;
}
#endif
