// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Tutorial/VBTutorialHintWidget.h"

#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Narrative/Tutorial/VBTutorialComponent.h"
#include "Narrative/Tutorial/VBTutorialDefinition.h"
#include "UI/VBInputKeyText.h"
#include "UI/VBUIManagerSubsystem.h"

UVBTutorialHintWidget::UVBTutorialHintWidget(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	SetIsFocusable(false);
}

void UVBTutorialHintWidget::NativeConstruct()
{
	Super::NativeConstruct();
	RefreshSnapshot();
	// 구성될 때마다 다시 잇는다(해체 때 Unbind).
	PlayerStateSet.FollowComponent(this, Tutorial, &UVBTutorialHintWidget::BindTo);
}

void UVBTutorialHintWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(false);
	if (TxtTitle)
	{
		TxtTitle->SetAutoWrapText(true);
	}

	if (TxtBody)
	{
		TxtBody->SetAutoWrapText(true);
	}
}

void UVBTutorialHintWidget::NativeDestruct()
{
	PlayerStateSet.Stop();
	Unbind();
	Super::NativeDestruct();
}

void UVBTutorialHintWidget::BindTo(UVBTutorialComponent* Component)
{
	Unbind();
	Tutorial = Component;
	if (Component)
	{
		ChangedHandle = Component->OnTutorialChanged.AddUObject(this, &UVBTutorialHintWidget::RefreshSnapshot);
		// 접힌 hint 는 NativeTick 으로 돌아올 수 없어 표시만 여기서 확인한다
		PresentationHandle = FTSTicker::GetCoreTicker().AddTicker(
			FTickerDelegate::CreateUObject(this, &UVBTutorialHintWidget::TickPresentation), 0.25f);
	}

	RefreshSnapshot();
}

void UVBTutorialHintWidget::Unbind()
{
	if (UVBTutorialComponent* Component = Tutorial.Get())
	{
		Component->OnTutorialChanged.Remove(ChangedHandle);
	}

	ChangedHandle.Reset();
	Tutorial.Reset();
	if (UVBUIManagerSubsystem* Manager = UIManager.Get())
	{
		Manager->OnInputAvailabilityChanged.Remove(InputAvailabilityHandle);
	}

	UIManager.Reset();
	InputAvailabilityHandle.Reset();
	FTSTicker::RemoveTicker(PresentationHandle);
	PresentationHandle.Reset();
	bHasHint = false;
	ShownStep = FVBTutorialStep();
	SetVisibility(ESlateVisibility::Collapsed);
}

void UVBTutorialHintWidget::RefreshSnapshot()
{
	const UVBTutorialComponent* Component = Tutorial.Get();
	const UVBTutorialDefinition* Definition = Component && Component->IsReady() ? Component->GetHintDefinition() : nullptr;
	const FVBTutorialProgress* Progress = Definition ? Component->FindProgress(Definition->TutorialId) : nullptr;
	const FVBTutorialStep* Step = Progress ? VBTutorial::GetHint(*Definition, *Progress) : nullptr;
	bHasHint = Step != nullptr;
	ShownStep = Step ? *Step : FVBTutorialStep();
	if (TxtTitle)
	{
		TxtTitle->SetText(ShownStep.Title);
	}

	RefreshPresentation();
}

void UVBTutorialHintWidget::RefreshPresentation()
{
	UVBUIManagerSubsystem* Manager = UVBUIManagerSubsystem::Get(this);
	if (Manager != UIManager.Get())
	{
		if (UVBUIManagerSubsystem* Previous = UIManager.Get())
		{
			Previous->OnInputAvailabilityChanged.Remove(InputAvailabilityHandle);
		}

		UIManager = Manager;
		InputAvailabilityHandle.Reset();
		if (Manager && Tutorial.IsValid())
		{
			InputAvailabilityHandle = Manager->OnInputAvailabilityChanged.AddUObject(
				this, &UVBTutorialHintWidget::RefreshPresentation);
		}
	}

	const FVBUIInputAvailability Availability = Manager
		? Manager->QueryInputAvailability(GetOwningPlayer()) : FVBUIInputAvailability();
	const bool bVisible = bHasHint && Tutorial.IsValid() && Tutorial->IsReady()
		&& Tutorial.Get() == UVBTutorialComponent::FindFor(GetOwningPlayer())
		&& Availability.bAvailable && !Availability.bBlocked;
	SetVisibility(bVisible ? ESlateVisibility::HitTestInvisible : ESlateVisibility::Collapsed);
	const ULocalPlayer* LocalPlayer = GetOwningLocalPlayer();
	if (TxtBody)
	{
		TxtBody->SetText(VBTutorial::BodyFor(ShownStep, VBInputKeyText::CurrentInputType(LocalPlayer)));
	}

	if (TxtKey || ImgKey)
	{
		VBInputKeyText::ShowKey(TxtKey, ImgKey, VBInputKeyText::ForCurrentDevice(LocalPlayer, ShownStep.InputMappingName),
			VBInputKeyText::IconForMapping(LocalPlayer, ShownStep.InputMappingName));
	}
}

bool UVBTutorialHintWidget::TickPresentation(float DeltaSeconds)
{
	if (!Tutorial.IsValid())
	{
		Unbind();
		return false;
	}

	RefreshPresentation();
	return true;
}
