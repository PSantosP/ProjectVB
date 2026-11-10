// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/Indicator/VBExecutePromptComponent.h"

#include "AbilitySystemGlobals.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/PlayerController.h"
#include "TimerManager.h"
#include "UI/Indicator/VBIndicatorRegistrySubsystem.h"
#include "UI/Indicator/VBRegisteredIndicatorsWidget.h"
#include "VB/VB.h"

UVBExecutePromptComponent::UVBExecutePromptComponent()
{
	// 매 틱 돌지 않는다 - 로컬 조종 중에만 짧은 타이머 하나로 묻는다.
	PrimaryComponentTick.bCanEverTick = false;
}

void UVBExecutePromptComponent::BeginPlay()
{
	Super::BeginPlay();
	// 그릴 위젯이 없으면 요청은 걸려도 화면에 아무것도 안 뜬다 - 조용한 미표시가 되지 않게 한 번 말한다.
	VB_CLOG(!PromptWidgetClass, Warning, "처형 프롬프트: %s 의 PromptWidgetClass 가 비었다 - 요청만 걸고 그릴 위젯이 없다",
		*GetNameSafe(GetOwner()));
	// 빙의는 BeginPlay 뒤에 오기도 한다 - 조종자가 바뀔 때마다 다시 맞춘다(서버 빙의, 해제, 클라 복제 모두 이 알림을 낸다).
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.AddUniqueDynamic(this, &UVBExecutePromptComponent::HandleControllerChanged);
	}

	UpdateArming();
}

void UVBExecutePromptComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (APawn* Pawn = Cast<APawn>(GetOwner()))
	{
		Pawn->ReceiveControllerChangedDelegate.RemoveDynamic(this, &UVBExecutePromptComponent::HandleControllerChanged);
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RefreshTimer);
	}

	ClearPrompt();
	Super::EndPlay(EndPlayReason);
}

void UVBExecutePromptComponent::HandleControllerChanged(APawn* /*Pawn*/, AController* /*OldController*/, AController* /*NewController*/)
{
	UpdateArming();
}

void UVBExecutePromptComponent::UpdateArming()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const APawn* Pawn = Cast<APawn>(GetOwner());
	const bool bLocal = Pawn && Pawn->IsLocallyControlled();
	FTimerManager& Timers = World->GetTimerManager();
	if (bLocal && !Timers.IsTimerActive(RefreshTimer))
	{
		Timers.SetTimer(RefreshTimer, FTimerDelegate::CreateUObject(this, &UVBExecutePromptComponent::RefreshPrompt),
			RefreshIntervalSeconds, /*InbLoop=*/true);
	}
	else if (!bLocal)
	{
		// 서버의 남의 폰, 원격 프록시는 묻지 않는다. 조종을 잃으면 걸어 둔 요청도 뗀다.
		Timers.ClearTimer(RefreshTimer);
		ClearPrompt();
	}
}

bool UVBExecutePromptComponent::IsPromptArmed() const
{
	const UWorld* World = GetWorld();
	return World && World->GetTimerManager().IsTimerActive(RefreshTimer);
}

AActor* UVBExecutePromptComponent::FindPromptTarget(APawn& Pawn) const
{
	// 처형 능력과 같은 질의 하나 - 사거리, 도달 규칙은 그 능력 설정의 것이다.
	return UVBAbilitySystemStatics::FindExecuteTarget(UAbilitySystemGlobals::GetAbilitySystemComponentFromActor(&Pawn));
}

void UVBExecutePromptComponent::RefreshPrompt()
{
	APawn* Pawn = Cast<APawn>(GetOwner());
	AActor* Target = (Pawn && Pawn->IsLocallyControlled()) ? FindPromptTarget(*Pawn) : nullptr;
	if (!Target)
	{
		ClearPrompt();
		return;
	}

	if (Target == PromptTarget.Get() && Registration.IsValid())
	{
		return;
	}

	UWorld* World = GetWorld();
	UVBIndicatorRegistrySubsystem* Registry = World ? World->GetSubsystem<UVBIndicatorRegistrySubsystem>() : nullptr;
	if (!Registry)
	{
		ClearPrompt();
		return;
	}

	FVBIndicatorRequest Request;
	Request.Group = VBGameplayTags::Indicator_Group_Execute;
	Request.AnchorComponent = Target->GetRootComponent();
	Request.WorldOffset = WorldOffset;
	Request.Subject = Target;
	Request.Data = PromptWidgetClass.Get();
	Request.ViewerScope = EVBIndicatorViewerScope::OneViewer;
	Request.Viewer = Cast<APlayerController>(Pawn->GetController());

	// 대상이 바뀌면 옛 요청을 떼고 새로 건다 - 하나의 프롬프트는 한 대상만 가리킨다.
	ClearPrompt();
	Registration = FVBIndicatorRegistration(*Registry, Request);
	if (Registration.IsValid())
	{
		PromptTarget = Target;
	}
}

void UVBExecutePromptComponent::ClearPrompt()
{
	Registration.Reset();
	PromptTarget.Reset();
}
