// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBStaminaBarWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/VBAbilityFailure.h"
#include "AbilitySystem/VBAbilitySystemComponent.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/ProgressBar.h"
#include "Engine/World.h"
#include "TimerManager.h"
#include "VB/VB.h"

void UVBStaminaBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 묶이기 전에는 보일 값이 없다.
	SetVisibility(ESlateVisibility::Collapsed);
	PlayerStateSet.FollowASC(this);
}

void UVBStaminaBarWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	Unbind();
	Super::NativeDestruct();
}

void UVBStaminaBarWidget::InitializeWithASC(UAbilitySystemComponent* ASC)
{
	if (ASC && ASC == BoundASC.Get())
	{
		return;
	}

	Unbind();
	if (ASC)
	{
		BoundASC = ASC;
		CurrentStamina = ASC->GetNumericAttribute(UVBCombatAttributeSet::GetStaminaAttribute());
		CurrentMaxStamina = ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxStaminaAttribute());
		StaminaChanged.Bind(*ASC, UVBCombatAttributeSet::GetStaminaAttribute(), this, &UVBStaminaBarWidget::OnStaminaChanged);
		MaxStaminaChanged.Bind(*ASC, UVBCombatAttributeSet::GetMaxStaminaAttribute(), this, &UVBStaminaBarWidget::OnMaxStaminaChanged);
		if (UVBAbilitySystemComponent* VBASC = Cast<UVBAbilitySystemComponent>(ASC))
		{
			FailureSource = VBASC;
			FailureHandle = VBASC->OnPlayerAbilityFailure.AddUObject(this, &UVBStaminaBarWidget::HandleAbilityFailure);
		}
	}
	// 묶는 순간 가득이면 기다리지 않고 숨는다 - 지연은 방금 다 찼음을 보이려는 것이다.
	Refresh();
	if (IsFull())
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UVBStaminaBarWidget::Unbind()
{
	StaminaChanged.Reset();
	MaxStaminaChanged.Reset();
	if (UVBAbilitySystemComponent* Old = FailureSource.Get())
	{
		Old->OnPlayerAbilityFailure.Remove(FailureHandle);
	}

	FailureSource.Reset();
	FailureHandle.Reset();
	BoundASC.Reset();
	CurrentStamina = 0.0f;
	CurrentMaxStamina = 0.0f;
	bFlashing = false;
	bWarnedNoMaxStamina = false;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HideTimer);
		World->GetTimerManager().ClearTimer(FlashTimer);
	}
}

void UVBStaminaBarWidget::OnStaminaChanged(const FOnAttributeChangeData& Data)
{
	CurrentStamina = Data.NewValue;
	Refresh();
}

void UVBStaminaBarWidget::OnMaxStaminaChanged(const FOnAttributeChangeData& Data)
{
	CurrentMaxStamina = Data.NewValue;
	Refresh();
}

void UVBStaminaBarWidget::HandleAbilityFailure(const FVBAbilityFailureReport& Report)
{
	if (!Report.FailureTags.HasTagExact(VBGameplayTags::Ability_Fail_Cost))
	{
		return;
	}

	WarnIfNoMaxStamina(TEXT("a cost refusal arrived"));
	bFlashing = true;
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(FlashTimer, this, &UVBStaminaBarWidget::EndRefusedFlash,
			FMath::Max(RefusedFlashSeconds, KINDA_SMALL_NUMBER), false);
	}
	// 거절의 이유가 이 바다 - 먼저 보이고 Refresh 가 숨기기를 정한다. 가득인데 거절이 와도(최대가 아직 0, 비용이 최대보다 큼)
	//   Refresh 가 숨기기 타이머를 건다. 순서를 바꾸면 접힌 바로 보고 타이머를 안 건 채 보이게만 남는다.
	SetVisibility(ESlateVisibility::HitTestInvisible);
	Refresh();
}

void UVBStaminaBarWidget::EndRefusedFlash()
{
	bFlashing = false;
	Refresh();
}

bool UVBStaminaBarWidget::IsFull() const
{
	// 최대가 0 이면 그릴 비율이 없어 가득으로 본다. 그 상태에서 기력 부족 거절이 오면 WarnIfNoMaxStamina 가 말한다.
	return CurrentMaxStamina <= 0.0f || CurrentStamina >= CurrentMaxStamina;
}

void UVBStaminaBarWidget::WarnIfNoMaxStamina(const TCHAR* Event)
{
	// 묶는 순간이나 값이 바뀌는 중의 0 은 경고하지 않는다 - 초기 능력치 효과가 늦게 오거나 Stamina 를 먼저 넣을 수 있다.
	//   0 인 채로 기력 부족 거절이 오면 초기화가 빠진 것이다.
	if (CurrentMaxStamina > 0.0f || bWarnedNoMaxStamina)
	{
		return;
	}

	bWarnedNoMaxStamina = true;
	VB_LOG(Warning, "[StaminaBar] %s: %s while MaxStamina is 0 - the bar cannot show it; check the player's initial stats effect",
		*GetNameSafe(BoundASC.Get() ? BoundASC->GetOwner() : nullptr), Event);
}

void UVBStaminaBarWidget::Refresh()
{
	const float Percent = CurrentMaxStamina > 0.0f ? FMath::Clamp(CurrentStamina / CurrentMaxStamina, 0.0f, 1.0f) : 1.0f;
	StaminaBar->SetPercent(Percent);
	StaminaBar->SetFillColorAndOpacity(bFlashing ? RefusedFillColor : (Percent < LowStaminaThreshold ? LowFillColor : NormalFillColor));

	UWorld* World = GetWorld();
	if (!IsFull())
	{
		if (World)
		{
			World->GetTimerManager().ClearTimer(HideTimer);
		}

		SetVisibility(ESlateVisibility::HitTestInvisible);
		return;
	}

	if (GetVisibility() != ESlateVisibility::Collapsed && World && !World->GetTimerManager().IsTimerActive(HideTimer))
	{
		World->GetTimerManager().SetTimer(HideTimer, this, &UVBStaminaBarWidget::HideWhenStillFull,
			FMath::Max(FullHideDelay, KINDA_SMALL_NUMBER), false);
	}
}

void UVBStaminaBarWidget::HideWhenStillFull()
{
	if (IsFull())
	{
		SetVisibility(ESlateVisibility::Collapsed);
	}
}
