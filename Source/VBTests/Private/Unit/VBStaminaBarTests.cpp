// ProjectVB Copyright (c) 2025. All Rights Reserved.

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"
#include "AbilitySystem/Abilities/VBGameplayAbility.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBAbilityFailure.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/ProgressBar.h"
#include "Data/VBHUDLayoutData.h"
#include "Player/VBPlayerState.h"
#include "TimerManager.h"
#include "UI/VBStaminaBarWidget.h"
#include "VBUITestFixture.h"

// 기력 바. 실제 WBP 를 실제 Slate 길로 구성, 해체한다(VBUITestFixture.h).
// 못 보는 것: 화면에 그려진 모양과 자리(PIE 몫), 원격 클라에서 회피 거절 사건이 오는지(이 시험 월드는 Standalone).
class FVBStaminaBarTestAccess
{
public:
	static bool IsHideTimerActive(const UVBStaminaBarWidget& Bar) { return Bar.GetWorld()->GetTimerManager().IsTimerActive(Bar.HideTimer); }
	// Fire - 타이머 만료와 같게 타이머를 끈 뒤 callback 을 부른다.
	static void FireHideTimer(UVBStaminaBarWidget& Bar) { Bar.GetWorld()->GetTimerManager().ClearTimer(Bar.HideTimer); Bar.HideWhenStillFull(); }
	static void FireFlashTimer(UVBStaminaBarWidget& Bar) { Bar.GetWorld()->GetTimerManager().ClearTimer(Bar.FlashTimer); Bar.EndRefusedFlash(); }
	static const UProgressBar* Fill(const UVBStaminaBarWidget& Bar) { return Bar.StaminaBar; }
	static FLinearColor Normal(const UVBStaminaBarWidget& Bar) { return Bar.NormalFillColor; }
	static FLinearColor Low(const UVBStaminaBarWidget& Bar) { return Bar.LowFillColor; }
	static FLinearColor Refused(const UVBStaminaBarWidget& Bar) { return Bar.RefusedFillColor; }
};

namespace
{
	using VBUITest::FRowFixture;
	using VBUITest::FSlateLife;

	void SetStamina(UAbilitySystemComponent& ASC, float Value)
	{
		ASC.SetNumericAttributeBase(UVBCombatAttributeSet::GetStaminaAttribute(), Value);
	}

	FVBAbilityFailureReport Failure(const FGameplayTag& Reason)
	{
		FVBAbilityFailureReport Report;
		Report.FailureTags.AddTag(Reason);
		return Report;
	}
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStaminaBarTest, "VB.Unit.HUD.StaminaBar",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBStaminaBarTest::RunTest(const FString& Parameters)
{
	using Access = FVBStaminaBarTestAccess;
	FRowFixture Fixture;
	if (!Fixture.Initialize(*this))
	{
		Fixture.Shutdown();
		return false;
	}

	AVBPlayerState* State = Fixture.SpawnPlayerState(*this);
	UVBAbilitySystemComponent* ASC = State ? Cast<UVBAbilitySystemComponent>(State->GetAbilitySystemComponent()) : nullptr;
	UVBStaminaBarWidget* Bar = Fixture.MakeRow<UVBStaminaBarWidget>(*this, TEXT("/Game/VB/UI/WBP_StaminaBar.WBP_StaminaBar_C"));
	if (!TestNotNull(TEXT("PlayerState ASC"), ASC) || !Bar || !TestNotNull(TEXT("fill"), Access::Fill(*Bar)))
	{
		Fixture.Shutdown();
		return false;
	}

	Fixture.SetPlayerState(State);

	// 초기 수치 전에 묶이면 MaxStamina 는 0 이다. 묶기는 조용하고, 0 일 때 쓴 stamina 는 묶음마다 한 번 경고한다.
	FSlateLife Life;
	Life.Construct(*Bar);
	const FOnGameplayAttributeValueChange& StaminaDelegate = ASC->GetGameplayAttributeValueChangeDelegate(UVBCombatAttributeSet::GetStaminaAttribute());
	TestTrue(TEXT("binds itself to the PlayerState ASC on construct"), StaminaDelegate.IsBoundToObject(Bar));
	TestTrue(TEXT("nothing to show before MaxStamina exists"), Bar->GetVisibility() == ESlateVisibility::Collapsed);
	AddExpectedMessagePlain(TEXT("while MaxStamina is 0"), ELogVerbosity::Warning, EAutomationExpectedMessageFlags::Contains, 1);
	ASC->OnPlayerAbilityFailure.Broadcast(Failure(VBGameplayTags::Ability_Fail_Cost));
	ASC->OnPlayerAbilityFailure.Broadcast(Failure(VBGameplayTags::Ability_Fail_Cost));
	Access::FireFlashTimer(*Bar);
	Access::FireHideTimer(*Bar);

	ASC->SetNumericAttributeBase(UVBCombatAttributeSet::GetMaxStaminaAttribute(), 100.0f);
	SetStamina(*ASC, 100.0f);
	Access::FireHideTimer(*Bar);
	TestTrue(TEXT("full -> hidden"), Bar->GetVisibility() == ESlateVisibility::Collapsed);

	SetStamina(*ASC, 40.0f);
	TestTrue(TEXT("a drop shows the bar"), Bar->GetVisibility() != ESlateVisibility::Collapsed);
	TestEqual(TEXT("percent follows stamina"), Access::Fill(*Bar)->GetPercent(), 0.4f, 0.001f);
	TestTrue(TEXT("normal colour above the low line"), Access::Fill(*Bar)->GetFillColorAndOpacity().Equals(Access::Normal(*Bar)));

	SetStamina(*ASC, 20.0f);
	TestTrue(TEXT("low colour below the line"), Access::Fill(*Bar)->GetFillColorAndOpacity().Equals(Access::Low(*Bar)));

	ASC->OnPlayerAbilityFailure.Broadcast(Failure(VBGameplayTags::Ability_Fail_TagsBlocked));
	TestTrue(TEXT("a non-cost refusal does not flash"), Access::Fill(*Bar)->GetFillColorAndOpacity().Equals(Access::Low(*Bar)));
	ASC->OnPlayerAbilityFailure.Broadcast(Failure(VBGameplayTags::Ability_Fail_Cost));
	TestTrue(TEXT("a cost refusal flashes"), Access::Fill(*Bar)->GetFillColorAndOpacity().Equals(Access::Refused(*Bar)));
	Access::FireFlashTimer(*Bar);
	TestTrue(TEXT("the flash ends on the low colour"), Access::Fill(*Bar)->GetFillColorAndOpacity().Equals(Access::Low(*Bar)));

	SetStamina(*ASC, 100.0f);
	TestTrue(TEXT("refilled bar stays a moment"), Bar->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("hide waits for the delay"), Access::IsHideTimerActive(*Bar));
	Access::FireHideTimer(*Bar);
	TestTrue(TEXT("hidden after the delay"), Bar->GetVisibility() == ESlateVisibility::Collapsed);

	// 막대가 가득일 때 (max 미초기화거나 비용이 max 초과) 비용 거절은 막대를 보였다가 다시 숨긴다.
	ASC->OnPlayerAbilityFailure.Broadcast(Failure(VBGameplayTags::Ability_Fail_Cost));
	TestTrue(TEXT("a refusal shows a hidden full bar"), Bar->GetVisibility() != ESlateVisibility::Collapsed);
	TestTrue(TEXT("a refusal at full still arms the hide"), Access::IsHideTimerActive(*Bar));
	Access::FireFlashTimer(*Bar);
	Access::FireHideTimer(*Bar);
	TestTrue(TEXT("a refusal at full hides again after the delay"), Bar->GetVisibility() == ESlateVisibility::Collapsed);

	SetStamina(*ASC, 90.0f);
	Access::FireHideTimer(*Bar);
	TestTrue(TEXT("a stale hide never hides a bar that is not full"), Bar->GetVisibility() != ESlateVisibility::Collapsed);

	Life.Destruct(*Bar);
	TestFalse(TEXT("destruct drops the stamina subscription"), StaminaDelegate.IsBoundToObject(Bar));
	TestFalse(TEXT("destruct drops the failure subscription"), ASC->OnPlayerAbilityFailure.IsBoundToObject(Bar));
	Fixture.Shutdown();
	return true;
}

// 막대에 필요한 실제 데이터 - HUD 슬롯과 widget, 그리고 거절 이벤트를 올리는 회피.
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FVBStaminaBarProductionAssetsTest, "VB.Unit.HUD.StaminaBarAssets",
	EAutomationTestFlags_ApplicationContextMask | EAutomationTestFlags::EngineFilter)
bool FVBStaminaBarProductionAssetsTest::RunTest(const FString& Parameters)
{
	const UVBHUDLayoutData* Layout = LoadObject<UVBHUDLayoutData>(nullptr, TEXT("/Game/VB/UI/DA_HUDLayout.DA_HUDLayout"));
	if (!TestNotNull(TEXT("DA_HUDLayout"), Layout))
	{
		return false;
	}

	const FGameplayTag Slot = VBGameplayTags::UI_Slot_HUD_Stamina;
	TestTrue(TEXT("stamina slot is placed"), Layout->Slots.ContainsByPredicate([&](const FVBUISlotPlacement& P) { return P.Slot == Slot; }));
	const FVBUISlotDefaultWidget* Row = Layout->Widgets.FindByPredicate([&](const FVBUISlotDefaultWidget& W) { return W.Slot == Slot; });
	if (TestNotNull(TEXT("stamina default widget row"), Row))
	{
		TestTrue(TEXT("row uses a stamina bar class"), Row->WidgetClass && Row->WidgetClass->IsChildOf(UVBStaminaBarWidget::StaticClass()));
		TestFalse(TEXT("row is required"), Row->bOptional);
	}

	const UClass* DodgeClass = StaticLoadClass(UGameplayAbility::StaticClass(), nullptr,
		TEXT("/Game/VB/GAS/Abilities/BP_GA_Dodge.BP_GA_Dodge_C"));
	const UVBGameplayAbility* Dodge = DodgeClass ? DodgeClass->GetDefaultObject<UVBGameplayAbility>() : nullptr;
	if (TestNotNull(TEXT("BP_GA_Dodge"), Dodge))
	{
		// stamina 로 거절된 회피는 막대를 깜빡이고, 다른 실패 줄은 조용하다.
		TestTrue(TEXT("dodge raises its refusal for the bar"), Dodge->bShowFailureToPlayer);
		TestNotNull(TEXT("dodge still spends stamina"), Dodge->GetCostGameplayEffect());
	}

	return true;
}
#endif
