// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "Misc/AutomationTest.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Abilities/GameplayAbility.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Character/VBWeaponStateComponent.h"
#include "VBGuardCancelTestTypes.h"
#include "VBPlayerBodyTestFixture.h"
#include "VBWeaponDefinitionTestUtil.h"

// 공격 중 가드를 누르면 그 공격을 끊고 가드가 켜진다. 무기가 준 가드가 아니면 켜지지 않고 스윙도 살아 있다.
// 반대 대조: 가드의 막힘 태그에 State.Combat.Attacking 을 되돌리거나 취소 태그에서 경공격을 빼면 해당 행이 실패해야 한다.
namespace VBGuardCancelsAttackTest
{
	// LoadShipped - 게임에서 손에 든 무기가 주는 실제 클래스를 읽는다. BP 가 태그를 덮으면 여기서 드러난다.
	UClass* LoadShipped(const TCHAR* Name)
	{
		return StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
			*FString::Printf(TEXT("/Game/VB/GAS/Abilities/%s.%s_C"), Name, Name));
	}

	bool IsActive(UAbilitySystemComponent& ASC, const FGameplayAbilitySpecHandle Handle)
	{
		const FGameplayAbilitySpec* Spec = ASC.FindAbilitySpecFromHandle(Handle);
		return Spec && Spec->IsActive();
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBGuardCancelsAttackTest, "VB.Unit.Combat.GuardCancelsAttack",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::ProductFilter)
bool FVBGuardCancelsAttackTest::RunTest(const FString& Parameters)
{
	using namespace VBGuardCancelsAttackTest;
	UClass* Guard = LoadShipped(TEXT("BP_GA_Guard"));
	if (!TestNotNull(TEXT("shipped BP_GA_Guard"), Guard))
	{
		return false;
	}

	// 시험의 스윙 모양이 실제 공격과 같은 식별자를 든다 - 비거나 바뀌면 취소가 소리 없이 빗나간다.
	const UClass* Light = LoadShipped(TEXT("BP_GA_LightAttack"));
	const UClass* Riposte = LoadShipped(TEXT("BP_GA_Riposte"));
	if (TestNotNull(TEXT("shipped BP_GA_LightAttack"), Light))
	{
		TestTrue(TEXT("the light attack carries Ability.Attack.Light"),
			Light->GetDefaultObject<UGameplayAbility>()->GetAssetTags().HasTagExact(VBGameplayTags::Ability_Attack_Light));
	}

	if (TestNotNull(TEXT("shipped BP_GA_Riposte"), Riposte))
	{
		TestTrue(TEXT("the riposte carries Ability.Attack.Riposte"),
			Riposte->GetDefaultObject<UGameplayAbility>()->GetAssetTags().HasTagExact(VBGameplayTags::Ability_Attack_Riposte));
	}

	VBPlayerBodyTest::FFixture F;
	if (!F.Initialize(*this))
	{
		return false;
	}

	F.PC->Possess(F.A);
	UAbilitySystemComponent& ASC = *F.ASC;
	const FGameplayAbilitySpecHandle LightHandle = ASC.GiveAbility(FGameplayAbilitySpec(UVBGuardCancelLightSwing::StaticClass()));
	const FGameplayAbilitySpecHandle RiposteHandle = ASC.GiveAbility(FGameplayAbilitySpec(UVBGuardCancelRiposteSwing::StaticClass()));
	UVBWeaponStateComponent* WSC = UVBWeaponStateComponent::FindFor(F.A);
	if (!TestNotNull(TEXT("body weapon state"), WSC))
	{
		return false;
	}
	// 무기 상태의 칸은 에디터 저작 칸이라 시험은 반사로만 채운다(제품 헤더에 시험 뒷문을 두지 않는다).
	UVBWeaponDefinition* Blade = VBWeaponDefinitionTest::Make(WSC, EVBWeaponType::Blade);
	VBWeaponDefinitionTest::Equip(*WSC, {Blade}, EVBWeaponType::Blade);

	// 무기 세트가 주지 않은 가드(SourceObject 가 무기 정의가 아님): 가드는 거절되고 도는 스윙은 그대로다.
	const FGameplayAbilitySpecHandle LooseGuard = ASC.GiveAbility(FGameplayAbilitySpec(Guard));
	if (TestTrue(TEXT("light swing starts"), ASC.TryActivateAbility(LightHandle)))
	{
		TestFalse(TEXT("guard not from a weapon: refused"), ASC.TryActivateAbility(LooseGuard));
		TestTrue(TEXT("guard not from a weapon: the swing keeps running"), IsActive(ASC, LightHandle));
		ASC.CancelAbilityHandle(LightHandle);
	}

	ASC.ClearAbility(LooseGuard);

	// 손에 든 무기가 준 가드.
	const FGameplayAbilitySpecHandle GuardHandle = ASC.GiveAbility(FGameplayAbilitySpec(Guard, 1, INDEX_NONE, Blade));

	TestTrue(TEXT("idle: guard activates"), ASC.TryActivateAbility(GuardHandle));
	TestTrue(TEXT("idle guard: the body is guarding"), F.A->IsGuarding());
	ASC.CancelAbilityHandle(GuardHandle);

	const TPair<const TCHAR*, FGameplayAbilitySpecHandle> Swings[] = {{TEXT("light"), LightHandle}, {TEXT("riposte"), RiposteHandle}};
	for (const TPair<const TCHAR*, FGameplayAbilitySpecHandle>& Swing : Swings)
	{
		// 반격은 패리가 연 창 안에서만 시작하고 (combat rule table), 그 창은 경공격을 막기도 한다.
		const bool bRiposte = Swing.Value == RiposteHandle;
		if (bRiposte)
		{
			ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
		}

		const bool bStarted = ASC.TryActivateAbility(Swing.Value);
		if (bRiposte)
		{
			ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Combat_RiposteWindow);
		}

		if (!TestTrue(*FString::Printf(TEXT("%s swing starts"), Swing.Key), bStarted))
		{
			continue;
		}

		TestTrue(*FString::Printf(TEXT("%s swing runs: the owner is attacking"), Swing.Key),
			ASC.HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking));
		TestTrue(*FString::Printf(TEXT("%s swing running: guard activates"), Swing.Key), ASC.TryActivateAbility(GuardHandle));
		TestFalse(*FString::Printf(TEXT("%s swing was cut by the guard"), Swing.Key), IsActive(ASC, Swing.Value));
		TestFalse(*FString::Printf(TEXT("%s swing cut: the owner is no longer attacking"), Swing.Key),
			ASC.HasMatchingGameplayTag(VBGameplayTags::State_Combat_Attacking));
		TestTrue(*FString::Printf(TEXT("%s swing cut: the body is guarding from the press"), Swing.Key), F.A->IsGuarding());
		ASC.CancelAbilityHandle(GuardHandle);
	}

	// 회피 중엔 여전히 가드가 켜지지 않는다.
	ASC.AddLooseGameplayTag(VBGameplayTags::State_Combat_Dodging);
	TestFalse(TEXT("dodging: guard refused"), ASC.TryActivateAbility(GuardHandle));
	ASC.RemoveLooseGameplayTag(VBGameplayTags::State_Combat_Dodging);
	return true;
}
#endif
