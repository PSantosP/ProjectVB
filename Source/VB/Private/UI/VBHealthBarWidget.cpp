// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "UI/VBHealthBarWidget.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"

void UVBHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	// 구성될 때마다 다시 잇는다. 같은 위젯이 해체 뒤 다시 구성돼도 바가 살아 있다.
	PlayerStateSet.FollowASC(this);
}

void UVBHealthBarWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	Super::NativeDestruct();
}

void UVBHealthBarWidget::OnASCBound(UAbilitySystemComponent& ASC)
{
	CurrentShield = ASC.GetNumericAttribute(UVBHealthAttributeSet::GetShieldAttribute());
	CurrentMaxShield = ASC.GetNumericAttribute(UVBHealthAttributeSet::GetMaxShieldAttribute());
	ShieldChanged.Bind(ASC, UVBHealthAttributeSet::GetShieldAttribute(), this, &UVBHealthBarWidget::OnShieldChanged);
	MaxShieldChanged.Bind(ASC, UVBHealthAttributeSet::GetMaxShieldAttribute(), this, &UVBHealthBarWidget::OnMaxShieldChanged);
	UpdateShieldBar();
}

void UVBHealthBarWidget::OnUnbound()
{
	ShieldChanged.Reset();
	MaxShieldChanged.Reset();
	// 저체력 맥동도 놓는다. 남겨 두면 다시 묶을 때 이미 저체력으로 읽어 맥동을 다시 켜지 않는다.
	if (bIsLowHealth && LowHealthPulse)
	{
		StopAnimation(LowHealthPulse);
	}

	bIsLowHealth = false;
}

void UVBHealthBarWidget::OnHealthRefreshed(float Percent)
{
	// 저체력 맥동 체크 - 묶을 때, 체력, 최대 체력 어느 쪽으로 넘어도 같은 판정이다.
	if (Percent <= LowHealthThreshold && !bIsLowHealth)
	{
		bIsLowHealth = true;
		if (LowHealthPulse)
		{
			PlayAnimation(LowHealthPulse, 0.0f, 0);
		}
	}
	else if (Percent > LowHealthThreshold && bIsLowHealth)
	{
		bIsLowHealth = false;
		if (LowHealthPulse)
		{
			StopAnimation(LowHealthPulse);
		}
	}
}

void UVBHealthBarWidget::OnHealthDropped()
{
	if (DamagePunch)
	{
		PlayAnimation(DamagePunch);
	}
}

void UVBHealthBarWidget::OnShieldChanged(const FOnAttributeChangeData& Data)
{
	CurrentShield = Data.NewValue;
	UpdateShieldBar();
}

void UVBHealthBarWidget::OnMaxShieldChanged(const FOnAttributeChangeData& Data)
{
	CurrentMaxShield = Data.NewValue;
	UpdateShieldBar();
}

void UVBHealthBarWidget::UpdateShieldBar()
{
	float Percent = CurrentShield / FMath::Max(CurrentMaxShield, 1.0f);
	ShieldBar->SetPercent(Percent);
}
