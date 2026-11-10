// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Containers/Ticker.h"   // FTSTicker::FDelegateHandle - PlayerState 늦음 진단
#include "GameFramework/PlayerController.h"
#include "GameplayTagContainer.h"
#include "Player/VBPlayerDeathSequence.h"
#include "Player/VBPlayerLoadingWatch.h"
#include "Player/VBPlayerScreens.h"
#include "UI/Loading/VBLoadingProcess.h"
#include "VBPlayerController.generated.h"


class UVBAbilitySystemComponent;
class UVBDialogueComponent;
class UVBShopComponent;
class UVBCraftingComponent;
class UVBFastTravelComponent;
class UVBPlayerRelocationComponent;
class UVBWeaponWheelComponent;
class UInputAction;
struct FGameplayTag;

DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnPlayerStateSet, APlayerState* /*PlayerState*/);

// 폰보다 오래 사는 조종자 연결점: 서버 호출 경계, PlayerState 도착 알림, 입력 바인딩을 맡는다.
// 화면 요청은 FVBPlayerScreens, 사망 표시는 FVBPlayerDeathSequence, 로딩 지켜보기는 FVBPlayerLoadingWatch 에 맡긴다.
UCLASS()
class VB_API AVBPlayerController : public APlayerController, public IVBLoadingProcess
{
	GENERATED_BODY()
	
public:
	AVBPlayerController();
	UVBWeaponWheelComponent* GetWeaponWheel() const { return WeaponWheel; }
	virtual void UpdateRotation(float DeltaTime) override;
	
	// PlayerState의 ASC에 접근하는 헬퍼
	UVBAbilitySystemComponent* GetVBAbilitySystemComponent() const;

	// 일시정지 메뉴 요청(HUD 의 Esc). 열어도 되는지는 PC 가, 화면, 정지, 입력 모드는 UI 층이 한다.
	void RequestPauseMenu();

	// 인벤토리, 퀘스트 일지, 스킬 트리 화면을 열어 달라는 요청(HUD 의 각 바인딩). 일시정지와 같은 문턱(CanOpenMenuScreen).
	void RequestInventoryScreen();
	void RequestJournalScreen();
	void RequestSkillTreeScreen();

	// 도감 화면 요청 - 사실을 주면 그 항목을 고른 채 연다. 해금은 화면이 Story 로 판단한다.
	UFUNCTION(BlueprintCallable, Category="VB|UI")
	void RequestCollectionScreen(FGameplayTag DiscoveryFact);

	// 지역에 들어왔다(서버가 들어온 플레이어에게만, 표시만이라 Unreliable). HUD 가 듣는다.
	UFUNCTION(Client, Unreliable)
	void ClientRegionEntered(FGameplayTag RegionId, bool bFirstVisit);

	DECLARE_MULTICAST_DELEGATE_TwoParams(FVBOnRegionEntered, FGameplayTag /*RegionId*/, bool /*bFirstVisit*/);
	FVBOnRegionEntered OnRegionEntered;

	// 화면 로드가 비동기라 고를 항목을 잠깐 보관한다. 화면이 켜질 때 한 번 가져간다(가져가면 비운다).
	FGameplayTag ConsumePendingCollectionEntry() { return Screens.ConsumeCollectionEntry(); }

	// 튜토리얼 다시 보기 요청 - ID 를 주면 그 안내를 고른 채 연다(읽기만).
	void RequestTutorialScreen(FGameplayTag TutorialId = FGameplayTag());
	FGameplayTag ConsumePendingTutorialId() { return Screens.ConsumeTutorialId(); }

	// 이미 켜진 화면에 고를 항목이 새로 왔다. 화면은 켜진 동안 구독하고 위 Consume 함수로 가져간다.
	FSimpleMulticastDelegate& OnScreenSelectionPending() { return Screens.OnSelectionPending; }

	// 일시정지, 게임오버에서 위임받는 레벨 전환(위젯 -> PC). 메뉴 화면을 내려 정지를 푼 뒤 OpenLevel(MainMenuLevel).
	void QuitToMainMenu();

	// 난이도 바꾸기 요청. 서버가 호스트 자신의 PC 인지 본다 - 원격 클라의 요청은 거절하고 말한다.
	UFUNCTION(Server, Reliable)
	void ServerRequestDifficulty(FGameplayTag Level);

	// 소유 폰 사망(로컬 조종일 때만): 사망 연출을 시작한다. 게임오버 화면은 사망 클립(게임초) + 여유 뒤에 뜬다.
	void OnPlayerDeath(float DeathAnimSeconds);

	// 게임오버 "계속하기":가장 최근 세이브를 로드해 재진입, 없거나 실패면 현재 레벨을 처음부터.
	void ContinueFromLastSave();

	// 칸의 키 액션(HUD 칸 줄이 키 이름을 읽는다). 없으면 nullptr.
	const UInputAction* GetQuickSlotAction(int32 SlotIndex) const;
	int32 GetQuickSlotActionCount() const { return QuickSlotActions.Num(); }

	// PlayerState 가 정해지면 부른다(이미 있으면 바로) - 원격 클라는 PC 보다 늦게 올 수 있다.
	FDelegateHandle CallAndRegister_OnPlayerStateSet(FVBOnPlayerStateSet::FDelegate Delegate);
	void UnregisterOnPlayerStateSet(FDelegateHandle Handle) { OnPlayerStateSet.Remove(Handle); }
	virtual void InitPlayerState() override;
	virtual void OnRep_PlayerState() override;

	// PlayerState 가 아직 없으면 경고한다(말만 한다, 계속 기다린다).
	void WarnIfPlayerStateLate() const;

	// 로딩 과정 - 로컬 권위 PC 만 등록한다. 판정은 VBPlayerLoadingRules 가 한다.
	virtual bool ShouldShowLoadingScreen(FString& OutReason) const override;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void SetPawn(APawn* InPawn) override;
	virtual void SetupInputComponent() override; // 퀵슬롯 키 + 디버그 키. 그 밖의 게임플레이 입력은 폰이, 일시정지 입력은 HUD 화면이 받는다.

#if !UE_BUILD_SHIPPING
	// 디버그 즉사(K, Shipping 제외) - raw BindKey(에셋을 만들지 않는다), Health 를 직접 0 으로(회피, 가드에 막히지 않게).
	void DebugKillSelf();
#endif

	// "계속하기"의 성공/실패 두 경로가 모두 여기로 수렴 - 메뉴 상태 해제 후 현재 레벨 재진입.
	void RestartCurrentLevel();

	// 세이브 로드 완료 콜백(1회용 구독). 성공/실패 무관하게 RestartCurrentLevel 로 수렴.
	UFUNCTION()
	void HandleContinueLoadCompleted(int32 SlotIndex, bool bSuccess);

	// Quit to Menu 가 열 메인 메뉴 레벨. 소프트 참조(레벨 리네임 시 갱신).
	UPROPERTY(EditDefaultsOnly, Category="VB|UI")
	TSoftObjectPtr<UWorld> MainMenuLevel;

	// 사망 슬로모 배율(1.0=정상). UVBTimeDilationSubsystem 으로만 건다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	float DeathSlowMoScale = 0.3f;

	// 슬로모 지속(실시간 초) - 타격 순간만 늦추고 나머지는 정상 속도로 쓰러진다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	float DeathSlowMoRealDuration = 0.9f;

	// 사망 애니 뒤 게임오버 화면까지의 여유(게임 초). 실시간 고정 지연으로 바꾸지 말 것 - 슬로모가 바뀌면 연출이 잘린다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	float DeathScreenExtraDelay = 0.5f;

	// 사망 애니 길이의 하한(게임 초) - 몽타주가 없어도 화면이 바로 뜨지 않게.
	UPROPERTY(EditDefaultsOnly, Category="VB|Death")
	float MinDeathScreenDelay = 1.0f;

	// 대화 진행을 관리한다(서버). 대화 화면은 이 PC 가 여닫는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	TObjectPtr<UVBDialogueComponent> DialogueComponent;

	// 거래 진행을 관리한다(서버). 상점 화면도 이 PC 가 여닫는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Shop")
	TObjectPtr<UVBShopComponent> ShopComponent;

	// 제작, 강화 진행을 관리한다(서버). 제작 화면도 이 PC 가 여닫는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Crafting")
	TObjectPtr<UVBCraftingComponent> CraftingComponent;

	// 빠른 이동 진행을 관리한다(서버). 목적지 화면도 이 PC 가 여닫는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|FastTravel")
	TObjectPtr<UVBFastTravelComponent> FastTravelComponent;

	// 이 플레이어를 어두운 화면 뒤로 옮긴다(서버). 빠른 이동과 스크립트 이동이 같이 쓴다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|FastTravel")
	TObjectPtr<UVBPlayerRelocationComponent> RelocationComponent;

	// 조종을 멈추지 않는 한 줄(혼잣말)을 이 플레이어 화면으로 보낸다 - HUD 자막이 듣는다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Dialogue")
	TObjectPtr<class UVBSpokenLineComponent> SpokenLineComponent;

	// 이 플레이어의 사건으로 알림 문구를 만든다(로컬). HUD 는 알림을 모른다.
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category="VB|Notice")
	TObjectPtr<class UVBNoticeProducerComponent> NoticeProducerComponent;

	// 퀵슬롯 키(번호 = 칸). PC 가 받는다 - 칸을 관리하는 쪽이 PlayerState 에 있어 폰과 수명이 다르다. HUD 칸 줄이 키 이름을 읽는다.
	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	TArray<TObjectPtr<UInputAction>> QuickSlotActions;

private:
	FVBOnPlayerStateSet OnPlayerStateSet;
	void BroadcastPlayerStateSet();

	// 시작 10초 뒤 티커가 부른다. 조용하다고 구독이 성공했다는 뜻은 아니다.
	bool ReportLatePlayerState(float DeltaSeconds);
	FTSTicker::FDelegateHandle LatePlayerStateCheckHandle;

	// 사망 연출의 조정값을 값 묶음으로 낸다.
	FVBPlayerDeathSettings MakeDeathSettings() const;

	// UpdateRotation 이 묻는 조종 중인 폰의 락온 - 폰이 바뀔 때 다시 받는다.
	TWeakObjectPtr<class UVBTargetLockComponent> PawnTargetLock;
	bool bWarnedPlayerStateClass = false;

	UPROPERTY(VisibleAnywhere, Category="VB|WeaponWheel")
	TObjectPtr<UVBWeaponWheelComponent> WeaponWheel;

	void HandleQuickSlotPressed(int32 SlotIndex);

	// 메뉴 화면을 열어도 되나(내 화면 + 사망 연출 중 아님) - 모든 화면 열기 요청의 같은 문턱.
	bool CanOpenMenuScreen() const;
	void RequestMenuScreen(FGameplayTag ScreenTag);

	// 화면 요청과 고를 항목.
	FVBPlayerScreens Screens;

	// 사망 표시 한 번과 이어하기 대기.
	FVBPlayerDeathSequence DeathSequence;

	// 로딩 과정 등록과 Pawn 준비 신호 구독.
	FVBPlayerLoadingWatch LoadingWatch;
};

/** PlayerState 신호에 건 등록 하나 - 건 PC 를 기억했다가 그 PC 에서 해제한다(해제할 때 소유 PC 를 다시 찾으면 못 찾을 수 있다). 다시 걸면 앞 등록을 먼저 해제한다. */
struct VB_API FVBPlayerStateSetRegistration
{
	void Register(AVBPlayerController& Controller, FVBOnPlayerStateSet::FDelegate Delegate);
	void Unregister();

private:
	TWeakObjectPtr<AVBPlayerController> RegisteredController;
	FDelegateHandle Handle;
};
