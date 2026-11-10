#include "UI/Combat/VBCinematicSkillGaugeWidget.h"

#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "CharacterCommon/VBCinematicSkillChargeComponent.h"
#include "CommonInputSubsystem.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Data/VBCinematicSkillChargeRules.h"
#include "Engine/LocalPlayer.h"
#include "EnhancedInputSubsystems.h"
#include "InputAction.h"
#include "UI/VBInputKeyText.h"
#include "VB/VB.h"

void UVBCinematicSkillGaugeWidget::NativeConstruct()
{
	Super::NativeConstruct();
	PlayerStateSet.FollowASC(this);
	if (ULocalPlayer* Player = GetOwningLocalPlayer())
	{
		InputSource = Player->GetSubsystem<UCommonInputSubsystem>();
		if (UCommonInputSubsystem* Input = InputSource.Get())
		{
			InputMethodHandle = Input->OnInputMethodChangedNative.AddUObject(this, &ThisClass::HandleInputMethodChanged);
		}

		MappingSource = Player->GetSubsystem<UEnhancedInputLocalPlayerSubsystem>();
		if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSource.Get())
		{
			Input->ControlMappingsRebuiltDelegate.AddUniqueDynamic(this, &ThisClass::RefreshKeyHint);
		}
	}

	Refresh();
}

void UVBCinematicSkillGaugeWidget::InitializeWithASC(UAbilitySystemComponent* ASC)
{
	if (ASC && SourceASC.Get() == ASC)
	{
		Refresh();
		return;
	}

	UnbindASC();
	if (ASC && ASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute())
		&& ASC->HasAttributeSetForAttribute(UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute()))
	{
		SourceASC = ASC;
		CurrentSubscription.Bind(*ASC, UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute(), this, &ThisClass::HandleAttributeChanged);
		MaximumSubscription.Bind(*ASC, UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute(), this,
			&ThisClass::HandleAttributeChanged);
		Charge = ASC->GetOwnerActor() ? ASC->GetOwnerActor()->FindComponentByClass<UVBCinematicSkillChargeComponent>() : nullptr;
		if (UVBCinematicSkillChargeComponent* Component = Charge.Get())
		{
			Component->OnGaugeStateChanged.AddUniqueDynamic(this, &ThisClass::Refresh);
		}
	}

	Refresh();
}

void UVBCinematicSkillGaugeWidget::UnbindASC()
{
	if (UVBCinematicSkillChargeComponent* Previous = Charge.Get())
	{
		Previous->OnGaugeStateChanged.RemoveDynamic(this, &ThisClass::Refresh);
	}

	Charge.Reset();
	CurrentSubscription.Reset();
	MaximumSubscription.Reset();
	SourceASC.Reset();
}

void UVBCinematicSkillGaugeWidget::HandleAttributeChanged(const FOnAttributeChangeData& Data) { Refresh(); }
void UVBCinematicSkillGaugeWidget::HandleInputMethodChanged(ECommonInputType Type) { RefreshKeyHint(); }

void UVBCinematicSkillGaugeWidget::Refresh()
{
	const UAbilitySystemComponent* ASC = SourceASC.Get();
	const float Current = ASC ? ASC->GetNumericAttribute(UVBCombatAttributeSet::GetCinematicSkillGaugeAttribute()) : 0.0f;
	const float Maximum = ASC ? ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxCinematicSkillGaugeAttribute()) : 0.0f;
	GaugePercent = FMath::IsFinite(Current) && FMath::IsFinite(Maximum) && Maximum > 0.0f
		? FMath::Clamp(Current / Maximum, 0.0f, 1.0f) : 0.0f;
	if (GaugeBar)
	{
		GaugeBar->SetPercent(GaugePercent);
	}

	if (ASC && Maximum <= 0.0f && Charge.IsValid() && Charge->IsUnlocked() && !bReportedNoMaximum)
	{
		// 해금됐는데 최대값이 없으면 init 효과가 안 채운 것이다 - 막대가 이유 없이 비어 보인다.
		VB_LOG(Warning, "%s: MaxCinematicSkillGauge is 0 while the skill is unlocked", *GetName());
		bReportedNoMaximum = true;
	}

	const bool bFull = UVBCinematicSkillChargeRules::IsFull(Current, Maximum);
	// Glow = 지금 스킬이 나간다. 그 판정은 스킬이 담당한다.
	const bool bShowGlow = bFull && Charge.IsValid() && Charge->IsReady();
	if (FullGlow)
	{
		if (bShowGlow && !IsAnimationPlaying(FullGlow))
		{
			PlayAnimation(FullGlow, 0.0f, 0);
		}
		else if (!bShowGlow)
		{
			StopAnimation(FullGlow);
		}
	}

	bGaugeFull = bFull;
	SetVisibility(Charge.IsValid() && Charge->IsUnlocked() ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	RefreshKeyHint();
}

void UVBCinematicSkillGaugeWidget::RefreshKeyHint()
{
	if (!KeyHint)
	{
		return;
	}

	const TOptional<ECommonInputType> Type = VBInputKeyText::CurrentInputType(GetOwningLocalPlayer());
	bool bUnmapped = false;
	FText Hint = Type.IsSet() && bGaugeFull && Charge.IsValid() && Charge->IsReady()
		? VBInputKeyText::ForAction(GetOwningPlayer(), SkillAction, Type.GetValue(), bUnmapped) : FText::GetEmpty();
	if (!Hint.IsEmpty() && Type.GetValue() == ECommonInputType::Gamepad)
	{
		const FText Chord = VBInputKeyText::ForAction(GetOwningPlayer(), PadChordAction, Type.GetValue(), bUnmapped);
		Hint = Chord.IsEmpty() ? FText::GetEmpty()
			: FText::Format(NSLOCTEXT("VBCinematicSkillGauge", "Chord", "{0} + {1}"), Chord, Hint);
	}

	KeyHint->SetText(Hint);
	KeyHint->SetVisibility(Hint.IsEmpty() ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
}

void UVBCinematicSkillGaugeWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	UnbindASC();
	if (UCommonInputSubsystem* Input = InputSource.Get())
	{
		Input->OnInputMethodChangedNative.Remove(InputMethodHandle);
	}

	if (UEnhancedInputLocalPlayerSubsystem* Input = MappingSource.Get())
	{
		Input->ControlMappingsRebuiltDelegate.RemoveDynamic(this, &ThisClass::RefreshKeyHint);
	}

	InputSource.Reset();
	MappingSource.Reset();
	InputMethodHandle.Reset();
	if (FullGlow)
	{
		StopAnimation(FullGlow);
	}

	GaugePercent = 0.0f;
	bGaugeFull = false;
	Super::NativeDestruct();
}
