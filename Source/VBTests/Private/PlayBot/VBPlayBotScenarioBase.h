// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "UObject/WeakObjectPtr.h"
#include "UObject/WeakObjectPtrTemplates.h"

class AActor;
class ACharacter;
class APawn;
class UWorld;
class APlayerController;
class UAbilitySystemComponent;
class UInputAction;
class UUserWidget;
class FVBPlayBotChecks;
class FVBPlayBotHumanModel;
class FVBPlayBotInputDriver;
class FVBPlayBotOverrides;
struct FVBPlayBotRunConfig;
struct FVBPlayBotPress;

enum class EVBPlayBotState : uint8
{
	Running,
	Succeeded,
	Failed
};

// 게임이 알린 피격 하나(지켜보는 actor 의 Event.Combat.Hit)
struct FVBPlayBotHit
{
	TWeakObjectPtr<AActor> Victim;
	TWeakObjectPtr<AActor> Instigator;
	bool bFromPlayer = false;
	// 타격 프레임의 플레이어 montage section 번호 + 1(콤보 타수), montage 가 없으면 INDEX_NONE
	int32 SectionHit = INDEX_NONE;
	FString Impact;
	float Damage = 0.f;
	int64 Frame = 0;
};

// 시나리오와 skill 이 쓸 수 있는 모든 것. 주인은 UVBPlayBotSubsystem 이고 시나리오 수명 동안 유효하다
struct FVBPlayBotContext
{
	UWorld* World = nullptr;
	APlayerController* PC = nullptr;
	ACharacter* Player = nullptr;
	UAbilitySystemComponent* PlayerASC = nullptr;
	int64 Frame = 0;

	FVBPlayBotHumanModel* Human = nullptr;
	FVBPlayBotInputDriver* Input = nullptr;
	FVBPlayBotChecks* Checks = nullptr;
	FVBPlayBotOverrides* Overrides = nullptr;
	const FVBPlayBotRunConfig* Config = nullptr;

	TFunction<void(const FString& Json)> Emit;
	// actor 의 피격, 추적 태그 이벤트를 telemetry 와 OnHit 에 묶는다
	TFunction<void(AActor* Actor)> Watch;
	// 시나리오가 넣는다 - 지켜보는 actor 의 피격마다 불린다
	TFunction<void(const FVBPlayBotHit& Hit)> OnHit;
	// 시나리오가 넣는다 - 예약한 버튼이 실제로 눌릴 때 불린다
	TFunction<void(const FVBPlayBotPress& Press)> OnPressInjected;
	// 프레임 줄에서 "t" 로 찍는 actor
	TWeakObjectPtr<ACharacter> Target;
	// 이번 프레임의 probe 결과(시나리오 tick 전에 runner 가 채운다)
	TArray<FString> NewFX;
	int32 DecalCount = 0;
	// 지켜보는 actor 별 플레이어 타격 수와 마지막 프레임(skill 이 사람처럼 반응한다)
	TMap<TWeakObjectPtr<AActor>, int32> HitsByPlayer;
	int64 LastPlayerHitFrame = -1;

	int64 MsToFrames(double Ms) const;
	int64 SecondsToFrames(double Seconds) const;
	// 이름으로 Input action 을 찾는다. GameplayTag("Input.Attack.Light")는 pawn hero 의 input config 에서, 그 밖은 player controller UPROPERTY 에서.
	//   옛 pawn slot 이름("AttackLightAction")은 nullptr 이라 아무것도 안 누르는 대신 setup 에서 실패한다.
	const UInputAction* FindAction(FName Name) const;
	// hero 가 이 pawn 에 마지막으로 적은 스틱(hero 나 로컬 입력이 없으면 0)
	static FVector2D RawMoveInput(const AActor* Pawn);
	// hero 의 GameplayCamera 에 자산이 있는가("활성인가" 가 아니다)
	static bool IsUsingGameplayCamera(const AActor* Pawn);
	// 월드 방향 -> control frame 의 스틱 축(x = 오른쪽, y = 앞)
	FVector2D ToStick(const FVector& WorldDirection) const;
	double Distance2D(const AActor* Other) const;
	static bool IsGrounded(const ACharacter* Character);
	// 플레이어 MaxRadius 안에서 Class(Tag 가 있으면 그것도) 의 가장 가까운 actor, 없으면 nullptr
	AActor* FindNearest(UClass* Class, FName Tag, double MaxRadius, UClass* Exclude = nullptr) const;
	// viewport 나 화면 위젯 트리 안의 첫 Class 위젯
	UUserWidget* FindWidget(UClass* Class) const;
	FString Label(const AActor* Actor) const;
};

// 점검, 측정 시나리오 하나. 하위 클래스는 VB_PLAYBOT_SCENARIO 로 이름 등록되고 ini 의 Class= 로 골라진다.
// Setup 은 패드가 살아난 뒤 녹화 전에 한 번, Tick 은 매 프레임 돈다.
class FVBPlayBotScenarioBase
{
public:
	virtual ~FVBPlayBotScenarioBase() = default;
	virtual bool Setup(FVBPlayBotContext& Ctx, FString& OutError) = 0;
	virtual EVBPlayBotState Tick(FVBPlayBotContext& Ctx) = 0;
	virtual void Teardown(FVBPlayBotContext& Ctx) {}
	// Why Tick returned Failed.
	const FString& GetFailure() const { return Failure; }

protected:
	EVBPlayBotState Fail(const FString& Reason)
	{
		Failure = Reason;
		return EVBPlayBotState::Failed;
	}

	FString Failure;
};

using FVBPlayBotScenarioFactory = TFunction<TUniquePtr<FVBPlayBotScenarioBase>()>;

struct FVBPlayBotScenarioRegistry
{
	static TMap<FName, FVBPlayBotScenarioFactory>& Get();
	static TUniquePtr<FVBPlayBotScenarioBase> Create(FName ClassName);
};

struct FVBPlayBotScenarioRegistrar
{
	FVBPlayBotScenarioRegistrar(const TCHAR* ClassName, FVBPlayBotScenarioFactory Factory)
	{
		FVBPlayBotScenarioRegistry::Get().Add(FName(ClassName), MoveTemp(Factory));
	}
};

#define VB_PLAYBOT_SCENARIO(Type, ClassName) \
	static FVBPlayBotScenarioRegistrar GVBPlayBotRegistrar_##Type(TEXT(ClassName), [] { return TUniquePtr<FVBPlayBotScenarioBase>(new Type()); });
