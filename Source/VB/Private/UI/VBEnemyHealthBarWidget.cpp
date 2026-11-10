// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBEnemyHealthBarWidget.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBCombatAttributeSet.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBEnemyBase.h"
#include "VB/VB.h"

bool UVBEnemyHealthBarWidget::ShowEnemy(AActor* Enemy)
{
	UAbilitySystemComponent* ASC = UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(Enemy);
	InitializeWithASC(ASC);
	if (!ASC)
	{
		return false;
	}

	if (TxtName)
	{
		const AVBEnemyBase* EnemyBase = Cast<AVBEnemyBase>(Enemy);
		TxtName->SetText(EnemyBase ? EnemyBase->GetEnemyConfig()->DisplayName : FText::GetEmpty());
	}

	return true;
}

void UVBEnemyHealthBarWidget::Clear()
{
	InitializeWithASC(nullptr);
}

void UVBEnemyHealthBarWidget::OnASCBound(UAbilitySystemComponent& ASC)
{
	Super::OnASCBound(ASC);
	if (!ASC.HasAttributeSetForAttribute(UVBCombatAttributeSet::GetPoiseAttribute()))
	{
		RefreshPosture();
		return;
	}

	VB_CLOG(!PostureBar, Warning, "적 체력 바 %s: PostureBar가 없어 자세 게이지를 표시할 수 없습니다", *GetName());
	PoiseChanged.Bind(ASC, UVBCombatAttributeSet::GetPoiseAttribute(), this, &ThisClass::HandlePostureChanged);
	MaxPoiseChanged.Bind(ASC, UVBCombatAttributeSet::GetMaxPoiseAttribute(), this, &ThisClass::HandlePostureChanged);
	CueSource = &ASC;
	WindowCueChangedHandle = ASC.RegisterGameplayTagEvent(VBGameplayTags::GameplayCue_Combat_FinisherWindow)
		.AddUObject(this, &ThisClass::HandleWindowCueChanged);
	RefreshPosture();
}

void UVBEnemyHealthBarWidget::OnUnbound()
{
	PoiseChanged.Reset();
	MaxPoiseChanged.Reset();
	if (UAbilitySystemComponent* Previous = CueSource.Get())
	{
		Previous->RegisterGameplayTagEvent(VBGameplayTags::GameplayCue_Combat_FinisherWindow).Remove(WindowCueChangedHandle);
	}

	CueSource.Reset();
	WindowCueChangedHandle.Reset();
	RefreshPosture();
	Super::OnUnbound();
}

void UVBEnemyHealthBarWidget::OnHealthRefreshed(float Percent)
{
	Super::OnHealthRefreshed(Percent);
	RefreshPosture();
}

void UVBEnemyHealthBarWidget::RefreshPosture()
{
	if (!PostureBar)
	{
		return;
	}

	const UAbilitySystemComponent* ASC = CueSource.Get();
	const float Maximum = ASC ? ASC->GetNumericAttribute(UVBCombatAttributeSet::GetMaxPoiseAttribute()) : 0.0f;
	const float Remaining = ASC ? ASC->GetNumericAttribute(UVBCombatAttributeSet::GetPoiseAttribute()) : 0.0f;
	const bool bVisible = ASC && FMath::IsFinite(Maximum) && Maximum > 0.0f && FMath::IsFinite(Remaining)
		&& ASC->GetNumericAttribute(UVBHealthAttributeSet::GetHealthAttribute()) > 0.0f;
	// 외곽선과 같은 복제 큐를 읽어, 자세 수치가 즉시 회복돼도 열린 처형 창을 표시한다.
	const bool bWindowVisible = ASC && ASC->HasMatchingGameplayTag(VBGameplayTags::GameplayCue_Combat_FinisherWindow);
	PostureBar->SetPercent(bVisible ? (bWindowVisible ? 1.0f : FMath::Clamp(1.0f - Remaining / Maximum, 0.0f, 1.0f)) : 0.0f);
	PostureBar->SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
}

void UVBEnemyHealthBarWidget::HandlePostureChanged(const FOnAttributeChangeData& Data)
{
	RefreshPosture();
}

void UVBEnemyHealthBarWidget::HandleWindowCueChanged(FGameplayTag Tag, int32 Count)
{
	RefreshPosture();
}
