// ProjectVB Copyright (c) 2025. All Rights Reserved.
#pragma once
#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "PlayBot/VBPlayBotNativeSteering.h"
class UWorld;
class ACharacter;
class APlayerController;
class UVBInteractableComponent;
class FVBPlayBotInputDriver;
class FVBPlayBotHumanModel;

enum class EVBPlayBotOpeningAction : uint8 { Interact, Walk, Jump, Draw, Fight, Observe, Push };

// 점프 입력 단계가 통과하려면 무엇을 봐야 하는지. Jump 는 공중에 뜬 것, Traverse 는 넘기 몽타주 단계,
// Ladder 는 사다리에 붙은 것. 같은 점프 입력이 창턱에서는 넘기, 사다리 앞에서는 사다리를 시작하므로
// 도착 위치만 보면 그냥 점프로 올라간 것과 구별되지 않는다.
enum class EVBPlayBotJumpEvidence : uint8 { Airborne, TraversalMove, Ladder };

// 정상 입력 단계의 목표, 결과 종류와 제한 시간을 보관한다.
struct FVBPlayBotOpeningStep
{
	FName Id;
	EVBPlayBotOpeningAction Kind = EVBPlayBotOpeningAction::Interact;
	EVBPlayBotJumpEvidence JumpEvidence = EVBPlayBotJumpEvidence::Airborne;
	FVector Point = FVector::ZeroVector;
	FName SaveKey;
	FString TargetPath;
	bool bTargetActivated = true;
	FString ClassPath;
	bool bWeaponAcquisition = false;
	FName DrawAction;
	FName FightGroup;
	FGameplayTag ObserveFact;
	double PushMinDistance = 0.0;
	double Timeout = 0.0;
	static bool Parse(const TArray<FString>& Rows, TArray<FVBPlayBotOpeningStep>& Out, FString& Error);
};

// 행동 판단에 필요한 읽기 전용 표본이다.
struct FVBPlayBotOpeningView
{
	FVector Position = FVector::ZeroVector;
	FVector Feet = FVector::ZeroVector;
	FVector Velocity = FVector::ZeroVector;
	FVector Camera = FVector::ZeroVector;
	FVector Target = FVector::ZeroVector;
	double CameraYaw = 0.0;
	double MoveYaw = 0.0;
	double FacingYaw = 0.0;
	bool bReady = false;
	bool bFocused = false;
	bool bActivated = false;
	bool bTargetComplete = false;
	bool bWeaponOwned = false;
	bool bWeaponDrawn = false;
	bool bObservedFact = false;
	bool bWeaponFact = false;
	bool bGrounded = false;
	uint8 MovementMode = 0;
	bool bTraversalMove = false;   // UVBTraversalComponent 가 넘기, 허들, 맨틀 몽타주 단계(Move)에 있다
	bool bOnLadder = false;        // 사다리 면에 붙어 있다
	bool bHolding = false;         // 플레이어가 밀기 대상을 잡고 있다(UVBInteractorComponent::GetPushable)
	double TargetDisplacement = 0.0; // 밀기 대상이 단계 시작 자리에서 움직인 수평 거리(cm)
};

// 대상 식별자와 게임 소유자의 결과를 읽으며 게임 상태는 바꾸지 않는다.
class FVBPlayBotHospitalObservation
{
public:
	bool Bind(UWorld* World, const FVBPlayBotOpeningStep& Step, FString& Error);
	bool Read(UWorld* World, FVBPlayBotOpeningView& Out, FString& Error) const;
private:
	TWeakObjectPtr<ACharacter> Player;
	TWeakObjectPtr<APlayerController> Controller;
	TWeakObjectPtr<UVBInteractableComponent> Target;
	FGameplayTag ReadyFact;
	FGameplayTag WeaponFact;
	bool bNeedsTarget = false;
	bool bNeedsWeapon = false;
	bool bTargetActivated = true;
	bool bPushTarget = false;
	FVector TargetStart = FVector::ZeroVector;
	FGameplayTag ObserveFact;
};

enum class EVBPlayBotOpeningResult : uint8 { Running, PartialComplete, Failed };

// 관측 값과 실제 입력만 사용하며 월드나 게임 상태를 수정할 수 없다.
class FVBPlayBotHospitalActions
{
public:
	EVBPlayBotOpeningResult Tick(const FVBPlayBotOpeningView& View, double Seconds,
		FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error);
	void NotifyInjected(int32 Id) { if (Id == PressId) { bInjected = true; } }
	FVBPlayBotOpeningStep Step;
	FName Action = TEXT("Input.Interact");
	double EffectTimeout = 0.0;
private:
	EVBPlayBotOpeningResult TickMovement(const FVBPlayBotOpeningView& View, double Seconds,
		FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error);
	EVBPlayBotOpeningResult TickPush(const FVBPlayBotOpeningView& View, double Seconds,
		FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human, FString& Error);
	bool Schedule(FName InputAction, double Seconds, FVBPlayBotInputDriver& Input, FVBPlayBotHumanModel& Human);
	void LookAtTarget(const FVBPlayBotOpeningView& View, FVBPlayBotInputDriver& Input) const;
	int32 PressId = INDEX_NONE;
	bool bSawOppositeState = false;
	bool bInjected = false;
	bool bSawFocus = false;
	bool bSawAirborne = false;
	bool bSawJumpEvidence = false;
	bool bSawSheathed = false;
	// 밀기: 잡기 -> 밀기 -> 놓기. 진행이 멈춘 시간을 재려고 가장 멀리 간 거리와 그 시각을 둔다.
	enum class EPushPhase : uint8 { Grab, Push, Release };
	EPushPhase PushPhase = EPushPhase::Grab;
	double PushBest = 0.0;
	double PushProgressSeconds = -1.0;
	double ActSeconds = -1.0;
	double CompleteSeconds = -1.0;
	FVBPlayBotNativeSteering Steering;
};
