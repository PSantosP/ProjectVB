// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "UI/VBPlayerStateBinding.h"

#include "AbilitySystemInterface.h"
#include "Blueprint/UserWidget.h"
#include "GameFramework/PlayerState.h"

void FVBPlayerStateBinding::Start(const UUserWidget& Widget, FVBOnPlayerStateSet::FDelegate Delegate)
{
	if (AVBPlayerController* Controller = Widget.GetOwningPlayer<AVBPlayerController>())
	{
		Registration.Register(*Controller, MoveTemp(Delegate));
	}
}

UAbilitySystemComponent* FVBPlayerStateBinding::FindASC(APlayerState* PlayerState)
{
	// ASC 가 없는 PlayerState 클래스는 PC 가 한 번 경고한다(BroadcastPlayerStateSet).
	const IAbilitySystemInterface* Source = Cast<IAbilitySystemInterface>(PlayerState);
	return Source ? Source->GetAbilitySystemComponent() : nullptr;
}
