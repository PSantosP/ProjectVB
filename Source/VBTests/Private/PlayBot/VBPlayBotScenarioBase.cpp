// ProjectVB Copyright (c) 2025. All Rights Reserved.

#include "PlayBot/VBPlayBotScenarioBase.h"

#include "PlayBot/VBPlayBotFrame.h"
#include "Character/VBHeroComponent.h"
#include "GameplayTagsManager.h"
#include "Blueprint/UserWidget.h"
#include "EngineUtils.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/PlayerController.h"
#include "InputAction.h"
#include "UObject/UObjectIterator.h"

namespace
{
	const UInputAction* ReadAction(const UObject* Owner, FName Property)
	{
		const FObjectProperty* Prop = Owner ? FindFProperty<FObjectProperty>(Owner->GetClass(), Property) : nullptr;
		return Prop ? Cast<UInputAction>(Prop->GetObjectPropertyValue_InContainer(Owner)) : nullptr;
	}
}

int64 FVBPlayBotContext::MsToFrames(double Ms) const
{
	return VBPlayBot::MsToFrames(Ms);
}

int64 FVBPlayBotContext::SecondsToFrames(double Seconds) const
{
	return MsToFrames(Seconds * 1000.0);
}

const UInputAction* FVBPlayBotContext::FindAction(FName Name) const
{
	const FGameplayTag Tag = UGameplayTagsManager::Get().RequestGameplayTag(Name, /*ErrorIfNotFound=*/false);
	if (Tag.IsValid())
	{
		const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Player);
		return Hero ? Hero->FindInputAction(Tag) : nullptr;
	}

	return ReadAction(PC, Name);
}

FVector2D FVBPlayBotContext::RawMoveInput(const AActor* Pawn)
{
	return UVBHeroComponent::GetRawMoveInputFor(Pawn);
}

bool FVBPlayBotContext::IsUsingGameplayCamera(const AActor* Pawn)
{
	const UVBHeroComponent* Hero = UVBHeroComponent::FindFor(Pawn);
	return Hero && Hero->IsUsingGameplayCamera();
}

FVector2D FVBPlayBotContext::ToStick(const FVector& WorldDirection) const
{
	if (!PC)
	{
		return FVector2D::ZeroVector;
	}

	const double Yaw = FMath::DegreesToRadians(PC->GetControlRotation().Yaw);
	const FVector2D Forward(FMath::Cos(Yaw), FMath::Sin(Yaw));
	const FVector2D Right(-FMath::Sin(Yaw), FMath::Cos(Yaw));
	const FVector2D Dir = FVector2D(WorldDirection).GetSafeNormal();
	return FVector2D(FVector2D::DotProduct(Dir, Right), FVector2D::DotProduct(Dir, Forward));
}

double FVBPlayBotContext::Distance2D(const AActor* Other) const
{
	return Player && Other ? FVector::Dist2D(Player->GetActorLocation(), Other->GetActorLocation()) : TNumericLimits<double>::Max();
}

bool FVBPlayBotContext::IsGrounded(const ACharacter* Character)
{
	const UCharacterMovementComponent* Move = Character ? Character->GetCharacterMovement() : nullptr;
	return Move && Move->IsMovingOnGround();
}

AActor* FVBPlayBotContext::FindNearest(UClass* Class, FName Tag, double MaxRadius, UClass* Exclude) const
{
	if (!World || !Player || !Class)
	{
		return nullptr;
	}

	AActor* Best = nullptr;
	double BestDistance = MaxRadius;
	for (TActorIterator<AActor> It(World, Class); It; ++It)
	{
		AActor* Actor = *It;
		if (Actor == Player || !IsValid(Actor) || (Exclude && Actor->IsA(Exclude)) || (!Tag.IsNone() && !Actor->ActorHasTag(Tag)))
		{
			continue;
		}

		const double Distance = FVector::Dist(Player->GetActorLocation(), Actor->GetActorLocation());
		if (Distance <= BestDistance)
		{
			Best = Actor;
			BestDistance = Distance;
		}
	}

	return Best;
}

UUserWidget* FVBPlayBotContext::FindWidget(UClass* Class) const
{
	if (!Class)
	{
		return nullptr;
	}

	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		// viewport 에 직접 있거나, 화면 트리 안에 지어진 자식(보스 바 같은 HUD 부품)
		if (IsValid(Widget) && Widget->IsA(Class) && Widget->GetWorld() == World
			&& (Widget->IsInViewport() || Widget->GetCachedWidget().IsValid()))
		{
			return Widget;
		}
	}

	return nullptr;
}

FString FVBPlayBotContext::Label(const AActor* Actor) const
{
	return Actor ? Actor->GetName() : FString(TEXT("none"));
}

TMap<FName, FVBPlayBotScenarioFactory>& FVBPlayBotScenarioRegistry::Get()
{
	static TMap<FName, FVBPlayBotScenarioFactory> Registry;
	return Registry;
}

TUniquePtr<FVBPlayBotScenarioBase> FVBPlayBotScenarioRegistry::Create(FName ClassName)
{
	const FVBPlayBotScenarioFactory* Factory = Get().Find(ClassName);
	return Factory ? (*Factory)() : nullptr;
}
