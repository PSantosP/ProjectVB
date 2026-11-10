// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Input/VBInputDeviceManager.h"
#include "EnhancedInputSubsystems.h"
#include "VB/VB.h"


UVBInputDeviceManager::UVBInputDeviceManager()
{
	PrimaryComponentTick.bCanEverTick = false;
}

UVBInputDeviceManager* UVBInputDeviceManager::FindFor(const AActor* Actor)
{
	return Actor ? Actor->FindComponentByClass<UVBInputDeviceManager>() : nullptr;
}

ECommonInputType UVBInputDeviceManager::GetCurrentDeviceType() const
{
	return CurrentDeviceType;
}

const UInputMappingContext* UVBInputDeviceManager::GetMappingContext(ECommonInputType DeviceType) const
{
	switch (DeviceType)
	{
	case ECommonInputType::MouseAndKeyboard: return IMC_KeyboardMouse;
	case ECommonInputType::Gamepad: return IMC_Gamepad;
	default: return nullptr;
	}
}

bool UVBInputDeviceManager::IsUsingGamepad() const
{
	return CurrentDeviceType == ECommonInputType::Gamepad;
}

void UVBInputDeviceManager::BeginPlay()
{
	Super::BeginPlay();

	APawn* Pawn = Cast<APawn>(GetOwner());
	if (!Pawn)
	{
		return;
	}

	// 입력 감지는 로컬 소유 Pawn 에서만 한다. 리슨 서버의 원격 Pawn 은 GetLocalPlayer() 가 없어 크래시한다.
	if (!Pawn->IsLocallyControlled())
	{
		return;
	}

	CachedPC = Cast<APlayerController>(Pawn->GetController());
	if (!CachedPC.IsValid())
	{
		return;
	}

	InitializeInputDetection();
}

void UVBInputDeviceManager::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	if (CachedPC.IsValid())
	{
		if (ULocalPlayer* LP = CachedPC->GetLocalPlayer())
		{
			if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
			{
				CommonInput->OnInputMethodChangedNative.RemoveAll(this);
			}
		}
	}

	Super::EndPlay(EndPlayReason);
}

void UVBInputDeviceManager::InitializeInputDetection()
{
	if (!CachedPC.IsValid())
	{
		return;
	}

	ULocalPlayer* LP = CachedPC->GetLocalPlayer();
	if (!LP)
	{
		return;
	}

	if (UCommonInputSubsystem* CommonInput = UCommonInputSubsystem::Get(LP))
	{
		CurrentGamepadName = CommonInput->GetCurrentGamepadName();
		// 현재 입력 타입으로 초기 IMC 설정
		ECommonInputType CurrentType = CommonInput->GetCurrentInputType();
		
		if (CurrentType == ECommonInputType::Gamepad)
		{
			SwitchToGamepad();
		}
		else
		{
			SwitchToKeyboardMouse();
		}
		// 입력 방식 변경 델리게이트 바인딩
		CommonInput->OnInputMethodChangedNative.AddUObject(
			this, &UVBInputDeviceManager::HandleInputMethodChanged);
	}
	else
	{
		// CommonInput 사용 불가 -> 기본 키보드/마우스 IMC 적용
		SwitchToKeyboardMouse();
	}
}

void UVBInputDeviceManager::HandleInputMethodChanged(ECommonInputType NewInputType)
{
	switch (NewInputType)
	{
		case ECommonInputType::MouseAndKeyboard:
			{
				if (CurrentDeviceType != ECommonInputType::MouseAndKeyboard)
				{
					SwitchToKeyboardMouse();
				}
			}
			break;
		case ECommonInputType::Gamepad:
			{
				const ULocalPlayer* LP = CachedPC.IsValid() ? CachedPC->GetLocalPlayer() : nullptr;
				const UCommonInputSubsystem* CommonInput = LP ? UCommonInputSubsystem::Get(LP) : nullptr;
				const FName NewGamepadName = CommonInput ? CommonInput->GetCurrentGamepadName() : NAME_None;
				if (CurrentDeviceType != ECommonInputType::Gamepad)
				{
					CurrentGamepadName = NewGamepadName;
					SwitchToGamepad();
				}
				else if (CurrentGamepadName != NewGamepadName)
				{
					// 같은 패드 종류에서 이름만 바뀌었다. IMC 는 그대로 두고 그림 구독자에게만 알린다.
					CurrentGamepadName = NewGamepadName;
					OnInputDeviceChanged.Broadcast(ECommonInputType::Gamepad);
					VB_LOG(Log, "Gamepad name changed to %s (IMC 유지)", *NewGamepadName.ToString());
				}
			}
			break;
		case ECommonInputType::Touch:
			{
				// 터치는 IMC 를 바꾸지 않는다. 화면 조작이 입력 액션에 직접 주입하고, 키보드, 패드도 계속 쓸 수 있다.
				if (CurrentDeviceType != ECommonInputType::Touch)
				{
					CurrentDeviceType = ECommonInputType::Touch;
					OnInputDeviceChanged.Broadcast(ECommonInputType::Touch);
					VB_LOG(Log, "Input switched to Touch (IMC 유지)");
				}
			}
			break;
		default:
			break;
	}
}

void UVBInputDeviceManager::SwitchToKeyboardMouse()
{
	// 새 IMC 추가 이전 IMC 제거
	ApplyMappingContext(IMC_KeyboardMouse, IMC_Gamepad);
	CurrentDeviceType = ECommonInputType::MouseAndKeyboard;
	OnInputDeviceChanged.Broadcast(ECommonInputType::MouseAndKeyboard);
	VB_LOG(Log, "Input switched to Keyboard/Mouse");
}

void UVBInputDeviceManager::SwitchToGamepad()
{
	// 새 IMC 추가 이전 IMC 제거
	ApplyMappingContext(IMC_Gamepad, IMC_KeyboardMouse);
	CurrentDeviceType = ECommonInputType::Gamepad;
	OnInputDeviceChanged.Broadcast(ECommonInputType::Gamepad);
	VB_LOG(Log, "Input switched to Gamepad");
}

void UVBInputDeviceManager::ApplyMappingContext(UInputMappingContext* NewIMC, UInputMappingContext* OldIMC)
{
	if (!CachedPC.IsValid())
	{
		return;
	}

	ULocalPlayer* LP = CachedPC->GetLocalPlayer();
	UEnhancedInputLocalPlayerSubsystem* EISubSystem =
			ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(LP);

	if (!EISubSystem)
	{
		return;
	}

	// 매핑을 지금 다시 짓는다(기본은 다음 틱). 뒤따르는 구독자가 새 장치의 키를 읽어야 한다.
	FModifyContextOptions Options;
	Options.bForceImmediately = true;

	// 이전 IMC 제거
	if (OldIMC)
	{
		EISubSystem->RemoveMappingContext(OldIMC, Options);
	}

	// 새 IMC 추가
	if (NewIMC)
	{
		EISubSystem->AddMappingContext(NewIMC, InputMappingPriority, Options);
	}
}
