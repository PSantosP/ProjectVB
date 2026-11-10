// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBBossHealthBarWidget.h"

#include "Components/PanelWidget.h"
#include "Data/VBEnemyConfig.h"
#include "Enemy/VBBossBase.h"
#include "Enemy/VBBossEncounterSubsystem.h"
#include "Engine/World.h"
#include "UI/VBEnemyHealthBarWidget.h"
#include "VB/VB.h"

void UVBBossHealthBarWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	BossBar = BarClass ? CreateWidget<UVBEnemyHealthBarWidget>(this, BarClass) : nullptr;
	if (!BossBar)
	{
		VB_LOG(Warning, "보스 바: BarClass 미지정(WBP_BossHealthBar 클래스 기본값) - 바가 뜨지 않는다");
		return;
	}

	BarSlot->AddChild(BossBar);
}

void UVBBossHealthBarWidget::NativeConstruct()
{
	Super::NativeConstruct();
	UVBBossEncounterSubsystem* Found = GetWorld() ? GetWorld()->GetSubsystem<UVBBossEncounterSubsystem>() : nullptr;
	if (!Found)
	{
		ShowBoss(nullptr);
		return;
	}

	Encounters = Found;
	EngagedBossChangedHandle = Found->OnEngagedBossChanged.AddUObject(this, &UVBBossHealthBarWidget::ShowBoss);
	ShowBoss(Found->GetEngagedBoss());
}

void UVBBossHealthBarWidget::NativeDestruct()
{
	if (UVBBossEncounterSubsystem* Old = Encounters.Get())
	{
		Old->OnEngagedBossChanged.Remove(EngagedBossChangedHandle);
	}

	Encounters.Reset();
	EngagedBossChangedHandle.Reset();
	Super::NativeDestruct();
}

void UVBBossHealthBarWidget::ShowBoss(AVBBossBase* Boss)
{
	if (!Boss || !BossBar || !BossBar->ShowEnemy(Boss))
	{
		if (BossBar)
		{
			BossBar->Clear();
		}

		SetVisibility(ESlateVisibility::Collapsed);
		return;
	}

	VB_CLOG(Boss->GetEnemyConfig()->DisplayName.IsEmpty(), Warning,
		"보스 바: %s 의 EnemyConfig(%s) 에 DisplayName 이 없다 - 이름 없이 바만 뜬다", *Boss->GetName(), *GetNameSafe(Boss->GetEnemyConfig()));
	SetVisibility(ESlateVisibility::HitTestInvisible);
}
