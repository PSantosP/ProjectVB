// ProjectVB Copyright (c) 2025. All Rights Reserved.


#include "Player/VBPlayerController.h"
#include "Player/VBWeaponWheelComponent.h"
#include "Camera/VBPlayerCameraManager.h"

#include "Player/VBPlayerState.h"
#include "AbilitySystemComponent.h"
#include "AbilitySystem/Attributes/VBHealthAttributeSet.h"
#include "Character/VBCharacter.h"
#include "Character/VBTargetLockComponent.h"
#include "Narrative/Dialogue/VBDialogueComponent.h"
#include "Narrative/Dialogue/VBSpokenLineComponent.h"
#include "UI/Notice/VBNoticeProducerComponent.h"
#include "Inventory/Shop/VBShopComponent.h"
#include "Inventory/Crafting/VBCraftingComponent.h"
#include "Map/VBFastTravelComponent.h"
#include "Map/VBPlayerRelocationComponent.h"
#include "Inventory/VBInventoryComponent.h"
#include "EnhancedInputComponent.h"
#include "InputAction.h"
#include "SaveSystem/VBSaveGameSubsystem.h"
#include "AbilitySystem/VBGameplayTags.h"
#include "Engine/World.h"
#include "CoreLoop/VBDifficultyStateComponent.h"
#include "Engine/LocalPlayer.h"
#include "Kismet/GameplayStatics.h"
#include "Settings/VBOptionsSubsystem.h"
#include "VB/VB.h"

AVBPlayerController::AVBPlayerController()
{
	WeaponWheel = CreateDefaultSubobject<UVBWeaponWheelComponent>(TEXT("WeaponWheel"));
	PlayerCameraManagerClass = AVBPlayerCameraManager::StaticClass();
	DialogueComponent = CreateDefaultSubobject<UVBDialogueComponent>(TEXT("Dialogue"));
	ShopComponent = CreateDefaultSubobject<UVBShopComponent>(TEXT("Shop"));
	CraftingComponent = CreateDefaultSubobject<UVBCraftingComponent>(TEXT("Crafting"));
	FastTravelComponent = CreateDefaultSubobject<UVBFastTravelComponent>(TEXT("FastTravel"));
	RelocationComponent = CreateDefaultSubobject<UVBPlayerRelocationComponent>(TEXT("Relocation"));
	SpokenLineComponent = CreateDefaultSubobject<UVBSpokenLineComponent>(TEXT("SpokenLine"));
	NoticeProducerComponent = CreateDefaultSubobject<UVBNoticeProducerComponent>(TEXT("NoticeProducer"));
}


void AVBPlayerController::UpdateRotation(float DeltaTime)
{
	if (WeaponWheel && WeaponWheel->IsOpen())
	{
		RotationInput = FRotator::ZeroRotator;
		return;
	}
	// 락온 입력은 이미 오프셋으로 수집됐다. 엔진의 pitch 제한/FaceRotation 전에 합성한다.
	if (IsLocalController())
	{
		if (UVBTargetLockComponent* Lock = PawnTargetLock.Get())
		{
			if (Lock->CanTrackCamera())
			{
				RotationInput = FRotator::ZeroRotator;
				Lock->UpdateCameraRotation(DeltaTime);
			}
			else
			{
				// 다른 시점 입력 경로도 우선하며 자유 시점의 RotationInput은 지우지 않는다.
				Lock->NotifyManualCameraInput(RotationInput.Yaw, RotationInput.Pitch);
				Lock->UpdateHitRecenter();
			}
		}
	}

	Super::UpdateRotation(DeltaTime);
}

namespace
{
	UVBOptionsSubsystem* FindOptions(const APlayerController& Controller)
	{
		const ULocalPlayer* LocalPlayer = Controller.GetLocalPlayer();
		return LocalPlayer ? LocalPlayer->GetSubsystem<UVBOptionsSubsystem>() : nullptr;
	}
}

void AVBPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// HUD 화면을 연다 - 입력 모드는 CommonUI 가 HUD 화면 설정으로 적용하고, 캐릭터 연결은 HUD 가 스스로 한다.
	if (IsLocalController())
	{
		if (UVBOptionsSubsystem* Options = FindOptions(*this))
		{
			Options->DifficultyRequest.BindUObject(this, &ThisClass::ServerRequestDifficulty);
		}

		Screens.OpenHUD(*this);
		Screens.FollowViews(*this, DialogueComponent, ShopComponent, CraftingComponent, FastTravelComponent);
		// 로컬 권위 PC 만 등록한다 - 원격 준비 상태는 알 수 없어 영원히 기다리게 된다.
		if (HasAuthority())
		{
			LoadingWatch.Register(*this);
		}

		if (!PlayerState)
		{
			// 실제 시간이라 일시정지, 슬로모와 무관하게 10초다.
			LatePlayerStateCheckHandle = FTSTicker::GetCoreTicker().AddTicker(
				FTickerDelegate::CreateUObject(this, &AVBPlayerController::ReportLatePlayerState), 10.0f);
		}
	}
}

void AVBPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	FTSTicker::RemoveTicker(LatePlayerStateCheckHandle);
	LatePlayerStateCheckHandle.Reset();
	LoadingWatch.Shutdown(*this);
	UVBOptionsSubsystem* Options = FindOptions(*this);
	if (Options && Options->DifficultyRequest.IsBoundToObject(this))
	{
		Options->DifficultyRequest.Unbind();
	}

	Super::EndPlay(EndPlayReason);
}

void AVBPlayerController::SetPawn(APawn* InPawn)
{
	Super::SetPawn(InPawn);
	PawnTargetLock = UVBTargetLockComponent::FindFor(InPawn);
	LoadingWatch.HandlePawnChanged(*this);
}

bool AVBPlayerController::ShouldShowLoadingScreen(FString& OutReason) const
{
	return VBPlayerLoadingRules::ShouldShowLoadingScreen(*this, OutReason);
}

bool AVBPlayerController::ReportLatePlayerState(float /*DeltaSeconds*/)
{
	LatePlayerStateCheckHandle.Reset();
	WarnIfPlayerStateLate();
	return false;
}

void AVBPlayerController::WarnIfPlayerStateLate() const
{
	VB_CLOG(!PlayerState, Warning, "[VBPC] 10초째 PlayerState 가 안 왔다 - 인벤토리·퀘스트·성장·튜토리얼·체력 줄이 비어 있다(계속 기다린다)");
}

FDelegateHandle AVBPlayerController::CallAndRegister_OnPlayerStateSet(FVBOnPlayerStateSet::FDelegate Delegate)
{
	if (PlayerState)
	{
		Delegate.ExecuteIfBound(PlayerState);
	}

	return OnPlayerStateSet.Add(MoveTemp(Delegate));
}

void FVBPlayerStateSetRegistration::Register(AVBPlayerController& Controller, FVBOnPlayerStateSet::FDelegate Delegate)
{
	Unregister();
	RegisteredController = &Controller;
	Handle = Controller.CallAndRegister_OnPlayerStateSet(MoveTemp(Delegate));
}

void FVBPlayerStateSetRegistration::Unregister()
{
	if (AVBPlayerController* Controller = RegisteredController.Get())
	{
		Controller->UnregisterOnPlayerStateSet(Handle);
	}

	RegisteredController.Reset();
	Handle.Reset();
}

// 서버, 호스트는 PC 가 만들어질 때(PostInitializeComponents) 여기서 PlayerState 를 얻는다.
void AVBPlayerController::InitPlayerState()
{
	Super::InitPlayerState();
	BroadcastPlayerStateSet();
}

// 원격 클라는 PlayerState 가 복제로 따로 온다 - PC 보다 늦을 수 있다.
void AVBPlayerController::OnRep_PlayerState()
{
	Super::OnRep_PlayerState();
	BroadcastPlayerStateSet();
}

// 비워지는 쪽(CleanupPlayerState)은 알리지 않는다 - 듣는 쪽은 부품 약참조가 비는 것으로 안다(Lyra CommonGame 도 null 을 방송하지 않는다).
void AVBPlayerController::BroadcastPlayerStateSet()
{
	if (PlayerState)
	{
		if (!PlayerState->IsA<AVBPlayerState>() && !bWarnedPlayerStateClass)
		{
			bWarnedPlayerStateClass = true;
			VB_LOG(Warning, "[VBPC] PlayerState(%s)가 AVBPlayerState 가 아니다 - 인벤토리·퀘스트·성장·튜토리얼·체력 줄이 영영 비어 있다(GameMode 의 PlayerStateClass)",
				*GetNameSafe(PlayerState->GetClass()));
		}

		OnPlayerStateSet.Broadcast(PlayerState);
	}
}

UVBAbilitySystemComponent* AVBPlayerController::GetVBAbilitySystemComponent() const
{
	// PlayerState에서 ASC를 가져옴
	if (const AVBPlayerState* PS = GetPlayerState<AVBPlayerState>())
	{
		return PS->GetVBAbilitySystemComponent();
	}

	return nullptr;
}

void AVBPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 게임플레이 입력은 폰(VBCharacter)이 바인드한다 - 퀵슬롯 키만 여기다(헤더 QuickSlotActions). 일시정지 입력은 HUD 화면이 CommonUI 로 받는다(RequestPauseMenu).
	if (UEnhancedInputComponent* EnhancedInput = Cast<UEnhancedInputComponent>(InputComponent))
	{
		WeaponWheel->BindInput(EnhancedInput);
		const UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(this);
		for (int32 SlotIndex = 0; SlotIndex < QuickSlotActions.Num(); ++SlotIndex)
		{
			if (QuickSlotActions[SlotIndex])
			{
				EnhancedInput->BindAction(QuickSlotActions[SlotIndex], ETriggerEvent::Started, this,
					&AVBPlayerController::HandleQuickSlotPressed, SlotIndex);
			}
		}
		// 칸 수는 인벤토리가 정한다. PlayerState 가 아직 없으면(SetupInputComponent 는 빙의보다 먼저 돌 수 있다) 견주지 못한다 - 누를 때 서버가 범위를 다시 본다.
		VB_CLOG(Inventory && QuickSlotActions.Num() != Inventory->GetQuickSlots().Num(), Warning,
			"PlayerController: 퀵슬롯 키 %d 개와 칸 %d 개가 다르다 - 키 없는 칸, 또는 칸 없는 키가 있다(QuickSlotActions / QuickSlotCount)",
			QuickSlotActions.Num(), Inventory ? Inventory->GetQuickSlots().Num() : 0);
	}
	else
	{
		VB_LOG(Warning,
			"PlayerController: InputComponent 가 Enhanced Input 이 아니다 - 퀵슬롯 키를 묶지 못했다(DefaultInput.ini DefaultInputComponentClass)");
	}
#if !UE_BUILD_SHIPPING
	// 디버그 즉사 키(raw BindKey). 사망 후에는 OnPlayerDeath 의 DisableInput 이 걷어 간다.
	if (InputComponent)
	{
		InputComponent->BindKey(EKeys::K, IE_Pressed, this, &AVBPlayerController::DebugKillSelf);
	}
#endif
}

const UInputAction* AVBPlayerController::GetQuickSlotAction(int32 SlotIndex) const
{
	return QuickSlotActions.IsValidIndex(SlotIndex) ? QuickSlotActions[SlotIndex].Get() : nullptr;
}

void AVBPlayerController::HandleQuickSlotPressed(int32 SlotIndex)
{
	// 판단은 서버가 한다(빈 칸, 가진 수, 조건). 여기서는 칸 번호만 보낸다.
	if (UVBInventoryComponent* Inventory = UVBInventoryComponent::FindFor(this))
	{
		Inventory->RequestUseQuickSlot(SlotIndex);
	}
	else
	{
		VB_LOG(Warning, "PlayerController: 퀵슬롯 %d - 인벤토리를 찾지 못했다(PlayerState 에 UVBInventoryComponent)", SlotIndex + 1);
	}
}

bool AVBPlayerController::CanOpenMenuScreen() const
{
	// 메뉴 화면은 내 화면에만, 사망 연출 중에는 안 연다(게임오버 화면이 뜨기 전 구간은 HUD 가 맨 위다).
	return IsLocalController() && !DeathSequence.IsActive();
}

void AVBPlayerController::RequestMenuScreen(FGameplayTag ScreenTag)
{
	if (CanOpenMenuScreen())
	{
		Screens.OpenMenu(*this, ScreenTag);
	}
}

void AVBPlayerController::RequestPauseMenu()
{
	// 정지는 화면 데이터(bPausesGame)를 보고 UI 관리자가 건다(SetPause 는 클라에서 조기 반환).
	RequestMenuScreen(VBGameplayTags::UI_Screen_Pause);
}

void AVBPlayerController::RequestInventoryScreen()
{
	RequestMenuScreen(VBGameplayTags::UI_Screen_Inventory);
}

void AVBPlayerController::RequestJournalScreen()
{
	RequestMenuScreen(VBGameplayTags::UI_Screen_Journal);
}

void AVBPlayerController::RequestSkillTreeScreen()
{
	RequestMenuScreen(VBGameplayTags::UI_Screen_SkillTree);
}

void AVBPlayerController::ClientRegionEntered_Implementation(FGameplayTag RegionId, bool bFirstVisit)
{
	OnRegionEntered.Broadcast(RegionId, bFirstVisit);
}

void AVBPlayerController::RequestCollectionScreen(FGameplayTag DiscoveryFact)
{
	if (CanOpenMenuScreen())
	{
		Screens.OpenCollection(*this, DiscoveryFact);
	}
}

void AVBPlayerController::RequestTutorialScreen(FGameplayTag TutorialId)
{
	if (CanOpenMenuScreen())
	{
		Screens.OpenTutorial(*this, TutorialId);
	}
}

void AVBPlayerController::ServerRequestDifficulty_Implementation(FGameplayTag Level)
{
	// 서버에서 IsLocalController 가 참인 PC 는 리슨 호스트(또는 싱글) 자신의 것뿐이다 - 원격 클라의 PC 는 거짓.
	if (!IsLocalController())
	{
		VB_LOG(Warning, "%s: 원격 플레이어의 난이도 %s 요청을 거절한다 - 난이도는 호스트만 바꾼다", *GetName(), *Level.ToString());
		return;
	}

	UVBDifficultyStateComponent* Difficulty = UVBDifficultyStateComponent::Get(this);
	VB_CLOG(!Difficulty, Warning, "%s: 이 월드에 난이도 주인이 없다 - 요청 %s 무시", *GetName(), *Level.ToString());
	if (Difficulty)
	{
		Difficulty->SetLevel(Level);
	}
}

void AVBPlayerController::QuitToMainMenu()
{
	// 메뉴 화면을 내려 정지를 푼다(정지 상태로 레벨 전환 금지). 메뉴 레벨의 입력 모드는 그 레벨의 메인 메뉴 화면이 정한다.
	Screens.CloseAllMenus(*this);

	if (!MainMenuLevel.IsNull())
	{
		// SP/호스트 전용 - 클라의 OpenLevel 은 세션을 조용히 떠난다(MP 확장 때 정식 끊기로).
		UGameplayStatics::OpenLevelBySoftObjectPtr(this, MainMenuLevel);
	}
	else
	{
		VB_LOG(Warning, "[VBPC] MainMenuLevel 미할당 — 메뉴 복귀 불가.");
	}
}

FVBPlayerDeathSettings AVBPlayerController::MakeDeathSettings() const
{
	FVBPlayerDeathSettings Settings;
	Settings.SlowMoScale = DeathSlowMoScale;
	Settings.SlowMoRealDuration = DeathSlowMoRealDuration;
	Settings.ScreenExtraDelay = DeathScreenExtraDelay;
	Settings.MinScreenDelay = MinDeathScreenDelay;
	return Settings;
}

void AVBPlayerController::OnPlayerDeath(float DeathAnimSeconds)
{
	DeathSequence.Begin(*this, Screens, MakeDeathSettings(), DeathAnimSeconds);
}

void AVBPlayerController::ContinueFromLastSave()
{
	if (DeathSequence.IsContinueInProgress())
	{
		return; // 중복 클릭으로 비동기 로드가 2회 겹치는 것 방지.
	}

	UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this);
	if (!Subsystem)
	{
		VB_LOG(Warning, "[VBPC] 세이브 서브시스템 없음 — 현재 레벨 재시작으로 폴백합니다.");
		RestartCurrentLevel();
		return;
	}

	// 정책은 서브시스템이 소유한다. 메인메뉴 Continue 와 반드시 같은 슬롯을 가리켜야 한다.
	const int32 RecentSlot = Subsystem->GetMostRecentSlotIndex();
	if (RecentSlot < 0)
	{
		// 세이브가 없으면 현재 레벨을 처음부터 - 새 PlayerState 가 시작 능력과 체력을 다시 준다.
		VB_LOG(Log, "[VBPC] 세이브 없음 — 현재 레벨을 처음부터 재시작합니다.");
		RestartCurrentLevel();
		return;
	}

	// 1회용 구독 후 비동기 로드 - 완료 콜백은 정지 중에도 온다(코어 티커).
	DeathSequence.SetContinueInProgress(true);
	Subsystem->OnGameLoadCompleted.AddDynamic(this, &AVBPlayerController::HandleContinueLoadCompleted);
	Subsystem->LoadGameSlot(RecentSlot);
}

void AVBPlayerController::HandleContinueLoadCompleted(int32 SlotIndex, bool bSuccess)
{
	if (UVBSaveGameSubsystem* Subsystem = UVBSaveGameSubsystem::Get(this))
	{
		Subsystem->OnGameLoadCompleted.RemoveDynamic(this, &AVBPlayerController::HandleContinueLoadCompleted);
	}

	DeathSequence.SetContinueInProgress(false);

	if (!bSuccess)
	{
		// 실패해도 화면에 갇히지 않는다 - 재시작으로 수렴(조용한 no-op 금지).
		VB_LOG(Warning, "[VBPC] 세이브 슬롯 %d 로드 실패 — 현재 레벨을 처음부터 재시작합니다.", SlotIndex);
	}

	// 성공, 실패 모두 여기로 - 성공이면 새 레벨의 GameMode 가 복원을 이어 한다.
	RestartCurrentLevel();
}

void AVBPlayerController::RestartCurrentLevel()
{
	// 정지 상태로 레벨 전환 금지 + 입력모드 복원(QuitToMainMenu 와 동일 계약).
	Screens.CloseAllMenus(*this);

	// 대상은 현재 레벨 - 세이브의 LastLevelName 은 쓰는 코드가 없어 늘 None 이다.
	const FString LevelName = UGameplayStatics::GetCurrentLevelName(this, /*bRemovePrefixString=*/true);
	if (LevelName.IsEmpty())
	{
		VB_LOG(Warning, "[VBPC] 현재 레벨명 해석 실패 — 레벨 재진입 불가.");
		return;
	}

	UGameplayStatics::OpenLevel(this, FName(*LevelName));
}

#if !UE_BUILD_SHIPPING
void AVBPlayerController::DebugKillSelf()
{
	AVBCharacter* VBChar = Cast<AVBCharacter>(GetPawn());
	if (!VBChar)
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: 조종 중인 VBCharacter 가 없습니다.");
		return;
	}

	// 어트리뷰트는 서버 권위다. 클라에서 눌러도 다음 복제에 덮여 무의미하므로 경고 없이 무시하지 않고 알린다.
	if (!VBChar->HasAuthority())
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: 권위 없음(클라이언트) — 무시합니다.");
		return;
	}

	UAbilitySystemComponent* ASC = VBChar->GetAbilitySystemComponent();
	if (!ASC)
	{
		VB_LOG(Warning, "[DEBUG] 즉사 키: ASC 가 없습니다.");
		return;
	}

	// Health 를 직접 0 으로 - 효과가 아닌 쓰기도 PostAttributeChange 를 지나 죽음의 입구에 닿는다.
	ASC->ApplyModToAttribute(UVBHealthAttributeSet::GetHealthAttribute(), EGameplayModOp::Override, 0.0f);
	VB_LOG(Warning, "[DEBUG] 즉사 키 — Health 0 적용: %s", *VBChar->GetName());
}
#endif
