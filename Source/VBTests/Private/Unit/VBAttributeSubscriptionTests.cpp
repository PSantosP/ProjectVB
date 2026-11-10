// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/Attributes/VBReputationAttributeSet.h"
#include "Player/VBPlayerState.h"
#include "UI/VBHealthBarWidget.h"
#include "UI/VBReputationBarWidget.h"
#include "VBUITestFixture.h"

// ASC attribute 를 듣는 위젯은 FVBAttributeSubscription 을 쥐고, 구독은 묶인 ASC 를 따라가다 해체 때 놓는다
// 대상 - PlayerState 로 묶이는 체력 바 보호막, 일시정지 메뉴가 열 때마다 묶어 주는 명성 바
namespace
{
	using VBUITest::FRowFixture;
	using VBUITest::FSlateLife;

	bool Listens(UAbilitySystemComponent* ASC, const FGameplayAttribute& Attribute, const UObject* Widget)
	{
		return ASC && ASC->GetGameplayAttributeValueChangeDelegate(Attribute).IsBoundToObject(Widget);
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBAttributeSubscriptionWidgetsTest, "VB.Unit.HUD.AttributeSubscriptions",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBAttributeSubscriptionWidgetsTest::RunTest(const FString& Parameters)
{
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	AVBPlayerState* StateA = Fixture.SpawnPlayerState(*this);
	AVBPlayerState* StateB = Fixture.SpawnPlayerState(*this);
	UVBHealthBarWidget* Health = Fixture.MakeRow<UVBHealthBarWidget>(*this, TEXT("/Game/VB/UI/WBP_HealthBar.WBP_HealthBar_C"));
	UVBReputationBarWidget* Reputation = Fixture.MakeRow<UVBReputationBarWidget>(*this,
		TEXT("/Game/VB/UI/WBP_ReputationBar.WBP_ReputationBar_C"));
	UAbilitySystemComponent* AscA = StateA ? StateA->GetAbilitySystemComponent() : nullptr;
	UAbilitySystemComponent* AscB = StateB ? StateB->GetAbilitySystemComponent() : nullptr;
	if (!Health || !Reputation || !TestNotNull(TEXT("ASC A"), AscA) || !TestNotNull(TEXT("ASC B"), AscB))
	{
		Fixture.Shutdown();
		return false;
	}

	const FGameplayAttribute Shield = UVBHealthAttributeSet::GetShieldAttribute();
	const FGameplayAttribute MaxShield = UVBHealthAttributeSet::GetMaxShieldAttribute();
	const FGameplayAttribute Rep = UVBReputationAttributeSet::GetReputationAttribute();

	// 체력 바 - 스스로 PlayerState ASC 에 묶고, PlayerState 가 바뀌면 따라가고, 해체 때 놓는다
	Fixture.SetPlayerState(StateA);
	FSlateLife HealthLife;
	HealthLife.Construct(*Health);
	TestTrue(TEXT("shield follows the bound ASC"), Listens(AscA, Shield, Health) && Listens(AscA, MaxShield, Health));
	Fixture.SetPlayerState(StateB);
	TestFalse(TEXT("old ASC loses the shield subscription"), Listens(AscA, Shield, Health) || Listens(AscA, MaxShield, Health));
	TestTrue(TEXT("new ASC gets the shield subscription"), Listens(AscB, Shield, Health) && Listens(AscB, MaxShield, Health));
	HealthLife.Destruct(*Health);
	TestFalse(TEXT("destruct drops the shield subscription"), Listens(AscB, Shield, Health) || Listens(AscB, MaxShield, Health));

	// 명성 바 - 주인이 열 때마다 ASC 를 주고, 두 번째 ASC 가 첫 것을 대신하고, 해체 때 놓는다
	FSlateLife RepLife;
	RepLife.Construct(*Reputation);
	Reputation->InitializeWithASC(AscA);
	Reputation->InitializeWithASC(AscA);
	TestTrue(TEXT("reputation listens to the given ASC"), Listens(AscA, Rep, Reputation));
	Reputation->InitializeWithASC(AscB);
	TestFalse(TEXT("old ASC loses the reputation subscription"), Listens(AscA, Rep, Reputation));
	TestTrue(TEXT("new ASC gets the reputation subscription"), Listens(AscB, Rep, Reputation));
	RepLife.Destruct(*Reputation);
	TestFalse(TEXT("destruct drops the reputation subscription"), Listens(AscB, Rep, Reputation));

	Fixture.Shutdown();
	return true;
}
#endif
