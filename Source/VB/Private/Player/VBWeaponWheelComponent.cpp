#include "Player/VBWeaponWheelComponent.h"
#include "Player/VBWeaponWheelRules.h"
#include "Character/VBCharacter.h"
#include "Character/VBWeaponStateComponent.h"
#include "Data/VBWeaponDefinition.h"
#include "AbilitySystem/VBAbilitySystemStatics.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "AbilitySystemComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Engine/LocalPlayer.h"
#include "Engine/World.h"
#include "Framework/Application/SlateApplication.h"
#include "GameFramework/PlayerController.h"
#include "TimeControl/VBTimeDilationSubsystem.h"
#include "UI/VBUIManagerSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "VB/VB.h"

UVBWeaponWheelComponent::UVBWeaponWheelComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	PrimaryComponentTick.bTickEvenWhenPaused = true;
}

TArray<FVBWeaponWheelEntry> UVBWeaponWheelComponent::CollectEntries(const UVBWeaponStateComponent& Weapons)
{
	TArray<FVBWeaponWheelEntry> Out;
	TSet<EVBWeaponType> Added;
	for (const UVBWeaponDefinition* Definition : Weapons.GetWeaponDefinitions())
	{
		const EVBWeaponType Type = Definition ? Definition->GetWeaponType() : EVBWeaponType::None;
		bool bAlreadyAdded = false;
		if (!Weapons.IsWeaponOwned(Type))
		{
			continue;
		}

		Added.Add(Type, &bAlreadyAdded);
		if (bAlreadyAdded)
		{
			continue;
		}

		FText Label = Definition->GetDisplayName();
		if (Label.IsEmpty())
		{
			VB_LOG(Warning,
				"\uBB34\uAE30 \uD720: %s \uC758 DisplayName \uC774 \uBE44\uC5B4 enum \uC774\uB984\uC73C\uB85C \uBCF4\uC778\uB2E4",
				*GetNameSafe(Definition));
			Label = StaticEnum<EVBWeaponType>()->GetDisplayNameTextByValue(static_cast<int64>(Type));
		}

		Out.Add({Type, Label});
	}

	return Out;
}

void UVBWeaponWheelComponent::BeginPlay()
{
	Super::BeginPlay();
	DilationHandle = FName(*FString::Printf(TEXT("WeaponWheel_%u"), GetUniqueID()));
	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()); Controller && Controller->IsLocalController())
	{
		Controller->OnPossessedPawnChanged.AddDynamic(this, &ThisClass::OnPawnChanged);
		UIManager = UVBUIManagerSubsystem::Get(Controller);
		if (UIManager.IsValid())
		{
			UIChangedHandle = UIManager->OnInputAvailabilityChanged.AddUObject(this, &ThisClass::CheckAvailability);
		}
	}
}

void UVBWeaponWheelComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Close(false);
	if (UIManager.IsValid())
	{
		UIManager->OnInputAvailabilityChanged.Remove(UIChangedHandle);
	}

	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		Controller->OnPossessedPawnChanged.RemoveDynamic(this, &ThisClass::OnPawnChanged);
	}

	Super::EndPlay(EndPlayReason);
}

void UVBWeaponWheelComponent::BindInput(UEnhancedInputComponent* Input)
{
	if (!Input || !WheelAction || !AimAction)
	{
		return;
	}

	Input->BindAction(WheelAction, ETriggerEvent::Started, this, &ThisClass::BeginHold);
	Input->BindAction(WheelAction, ETriggerEvent::Triggered, this, &ThisClass::Open);
	Input->BindAction(WheelAction, ETriggerEvent::Completed, this, &ThisClass::Complete);
	Input->BindAction(WheelAction, ETriggerEvent::Canceled, this, &ThisClass::Cancel);
	Input->BindAction(AimAction, ETriggerEvent::Triggered, this, &ThisClass::Aim);
}

void UVBWeaponWheelComponent::SetPresentation(UUserWidget* Widget)
{
	if (Presentation.Get() != Widget)
	{
		Close(false);
	}

	Presentation = Widget;
}

bool UVBWeaponWheelComponent::CanOpen() const
{
	const APlayerController* Controller = Cast<APlayerController>(GetOwner());
	const AVBCharacter* Character = Controller ? Cast<AVBCharacter>(Controller->GetPawn()) : nullptr;
	if (!Controller || !Controller->IsLocalController() || !IsValid(Character)
		|| !Presentation.IsValid() || !AimContext || !UIManager.IsValid()
		|| (bOpen && OpenedPawn.Get() != Character) || UVBAbilitySystemStatics::IsActorFinishing(Character))
	{
		return false;
	}

	const FVBUIInputAvailability Availability = UIManager->QueryInputAvailability(Controller);
	if (!Availability.bAvailable || Availability.bBlocked)
	{
		return false;
	}

	const UAbilitySystemComponent* ASC = Character->GetAbilitySystemComponent();
	return ASC && !UVBAbilitySystemStatics::IsDead(ASC)
		&& !ASC->HasMatchingGameplayTag(VBGameplayTags::State_Interacting);
}

void UVBWeaponWheelComponent::BeginHold()
{
	bHoldAllowed = CanOpen();
	VB_LOG(Verbose, "WeaponWheel hold allowed=%d presentation=%d ui=%d", bHoldAllowed, Presentation.IsValid(), UIManager.IsValid());
}

void UVBWeaponWheelComponent::Open()
{
	if (bOpen || !bHoldAllowed || !CanOpen())
	{
		return;
	}

	APlayerController* Controller = CastChecked<APlayerController>(GetOwner());
	AVBCharacter* Character = CastChecked<AVBCharacter>(Controller->GetPawn());
	UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Character);
	UEnhancedInputLocalPlayerSubsystem* Input =
		ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer());
	// 플레이어 캐릭터는 무기 상태를 늘 만든다(AVBCharacter 생성자). 없으면 조립이 깨진 것이라 휠이 조용히 안 열리지 않게 알린다.
	if (!Weapons)
	{
		VB_LOG(Error, "무기 휠: %s 에 무기 상태 컴포넌트가 없다 - 열지 않는다", *GetNameSafe(Character));
	}

	if (!Weapons || !Input)
	{
		return;
	}

	VisibleEntries = CollectEntries(*Weapons);
	if (VisibleEntries.IsEmpty())
	{
		return;
	}

	OpenedPawn = Character;
	Cursor = FVector2D::ZeroVector;
	SelectedSlot = INDEX_NONE;
	bOpen = true;
	VB_LOG(Verbose, "WeaponWheel open entries=%d", VisibleEntries.Num());
	FModifyContextOptions Options;
	Options.bIgnoreAllPressedKeysUntilRelease = false;
	Input->AddMappingContext(AimContext, AimPriority, Options);
	if (GetWorld()->GetNetMode() == NM_Standalone)
	{
		if (UVBTimeDilationSubsystem* Time = UVBTimeDilationSubsystem::Get(this))
		{
			Time->Push(DilationHandle, FMath::Clamp(SlowMotionScale, 0.01f, 1.f));
		}
	}

	SetComponentTickEnabled(true);
	OnChanged.Broadcast();
}

void UVBWeaponWheelComponent::Close(bool bCommit)
{
	bHoldAllowed = false;
	if (!bOpen)
	{
		return;
	}

	VB_LOG(Verbose, "WeaponWheel close commit=%d allowed=%d slot=%d", bCommit, CanOpen(), SelectedSlot);
	AVBCharacter* Character = OpenedPawn.Get();
	const EVBWeaponType Selected = bCommit && CanOpen() && VisibleEntries.IsValidIndex(SelectedSlot)
		? VisibleEntries[SelectedSlot].Weapon : EVBWeaponType::None;
	bOpen = false;
	SetComponentTickEnabled(false);
	if (APlayerController* Controller = Cast<APlayerController>(GetOwner()))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Input =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(Controller->GetLocalPlayer()))
		{
			Input->RemoveMappingContext(AimContext);
		}
	}

	if (UVBTimeDilationSubsystem* Time = UVBTimeDilationSubsystem::Get(this))
	{
		Time->Pop(DilationHandle);
	}

	OpenedPawn.Reset();
	Cursor = FVector2D::ZeroVector;
	SelectedSlot = INDEX_NONE;
	OnChanged.Broadcast();
	if (IsValid(Character) && Selected != EVBWeaponType::None)
	{
		if (UVBWeaponStateComponent* Weapons = UVBWeaponStateComponent::FindFor(Character))
		{
			Weapons->SummonWeapon(Selected);
		}
	}
}

void UVBWeaponWheelComponent::Complete() { Close(true); }
void UVBWeaponWheelComponent::Cancel() { Close(false); }
void UVBWeaponWheelComponent::OnPawnChanged(APawn* OldPawn, APawn* NewPawn) { Close(false); }

void UVBWeaponWheelComponent::Aim(const FInputActionValue& Value)
{
	if (!bOpen)
	{
		return;
	}

	const FVector2D Delta = Value.Get<FVector2D>();
	Cursor = VBWeaponWheel::AccumulateCursor(Cursor, FVector2D(Delta.X, -Delta.Y) * MouseSensitivity, CursorRadius);
	SelectedSlot = VBWeaponWheel::PickSlot(Cursor, VisibleEntries.Num(), DeadRadius);
	OnChanged.Broadcast();
}

void UVBWeaponWheelComponent::CheckAvailability()
{
	if (bOpen && !CanOpen())
	{
		Close(false);
	}
}

void UVBWeaponWheelComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);
	CheckAvailability();
	if (bOpen && FSlateApplication::IsInitialized() && !FSlateApplication::Get().IsActive())
	{
		VB_LOG(Verbose, "WeaponWheel focus lost");
		Close(false);
	}
}
