// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBAttributeBarWidget.h"

#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Engine/World.h"
#include "TimerManager.h"

void UVBAttributeBarWidget::NativeDestruct()
{
	UnbindASC();
	Super::NativeDestruct();
}

void UVBAttributeBarWidget::InitializeWithASC(UAbilitySystemComponent* ASC)
{
	if (ASC && ASC == BoundASC.Get())
	{
		return;
	}

	UnbindASC();
	if (!ASC)
	{
		return;
	}

	BoundASC = ASC;

	CurrentHealth = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute());
	CurrentMaxHealth = ASC->GetNumericAttribute(UVBHealthAttributeSet::GetMaxHealthAttribute());
	// 구독이 ASC 와 핸들을 같이 든다 - 소스가 바뀌거나 해체될 때 그것이 뗀다.
	HealthChanged.Bind(*ASC, UVBHealthAttributeSet::GetHealthAttribute(), this, &UVBAttributeBarWidget::OnHealthChanged);
	MaxHealthChanged.Bind(*ASC, UVBHealthAttributeSet::GetMaxHealthAttribute(), this, &UVBAttributeBarWidget::OnMaxHealthChanged);
	OnASCBound(*ASC);

	RefreshHealth();
	HealthBarDelay->SetPercent(CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f));
}

void UVBAttributeBarWidget::UnbindASC()
{
	BoundASC.Reset();
	HealthChanged.Reset();
	MaxHealthChanged.Reset();
	// 잔상 바 타이머는 옛 값으로 바를 움직인다 - 소스를 놓으면 같이 멈춘다.
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DelayBarTimerHandle);
	}

	OnUnbound();
}

void UVBAttributeBarWidget::RefreshHealth()
{
	const float Percent = CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f);
	HealthBar->SetPercent(Percent);
	if (HealthText)
	{
		HealthText->SetVisibility(
			HealthNumberFormat == EVBBarNumberFormat::Hidden ? ESlateVisibility::Collapsed : ESlateVisibility::HitTestInvisible);
		HealthText->SetText(FormatHealthNumber(CurrentHealth, CurrentMaxHealth, HealthNumberFormat));
	}

	OnHealthRefreshed(Percent);
}

void UVBAttributeBarWidget::SetHealthNumberFormat(EVBBarNumberFormat Format)
{
	HealthNumberFormat = Format;
	RefreshHealth();
}

FText UVBAttributeBarWidget::FormatHealthNumber(float Health, float MaxHealth, EVBBarNumberFormat Format)
{
	const int32 Max = (FMath::IsFinite(MaxHealth) && MaxHealth > 0.0f) ? FMath::RoundToInt(MaxHealth) : 0;
	int32 Current = (FMath::IsFinite(Health) && Health > 0.0f) ? FMath::CeilToInt(Health) : 0;
	// 올림이 보이는 최대를 넘지 않게(가득 찬 119.4 가 120 / 119 로 읽히지 않게). 강화 배율로 최대에 소수가 생긴다.
	if (Max > 0)
	{
		Current = FMath::Min(Current, Max);
	}

	switch (Format)
	{
	case EVBBarNumberFormat::Current:
		return FText::AsNumber(Current);
	case EVBBarNumberFormat::CurrentAndMax:
		return FText::Format(NSLOCTEXT("VBAttributeBar", "HealthNumber", "{0} / {1}"), FText::AsNumber(Current), FText::AsNumber(Max));
	default:
		return FText::GetEmpty();
	}
}

void UVBAttributeBarWidget::StartDelayBar()
{
	GetWorld()->GetTimerManager().SetTimer(
		DelayBarTimerHandle, this, &UVBAttributeBarWidget::TickDelayBar,
		DelayBarUpdateInterval,
		true,
		DelayBarStartDelay);
}

void UVBAttributeBarWidget::TickDelayBar()
{
	const float HealthPercent = CurrentHealth / FMath::Max(CurrentMaxHealth, 1.0f);
	// 딜레이 바를 HP 바 쪽으로 천천히 보간
	const float NewPercent = FMath::FInterpTo(HealthBarDelay->GetPercent(), HealthPercent, DelayBarUpdateInterval, DelayBarInterpSpeed);
	HealthBarDelay->SetPercent(NewPercent);

	// 거의 같아지면 정지
	if (FMath::IsNearlyEqual(NewPercent, HealthPercent, 0.01f))
	{
		HealthBarDelay->SetPercent(HealthPercent);
		GetWorld()->GetTimerManager().ClearTimer(DelayBarTimerHandle);
	}
}

void UVBAttributeBarWidget::OnHealthChanged(const FOnAttributeChangeData& Data)
{
	CurrentHealth = Data.NewValue;
	RefreshHealth();
	StartDelayBar();
	if (Data.NewValue < Data.OldValue)
	{
		OnHealthDropped();
	}
}

void UVBAttributeBarWidget::OnMaxHealthChanged(const FOnAttributeChangeData& Data)
{
	CurrentMaxHealth = Data.NewValue;
	RefreshHealth();
}
