// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "GameplayTagContainer.h"
#include "VBHeroComponent.generated.h"

class AVBCharacter;
class UAbilitySystemComponent;
class UEnhancedInputComponent;
class UInputAction;
class UInputComponent;
class UVBGASPSmartObjectComponent;
class UVBInteractorComponent;
class UVBLocomotionIntentComponent;
class UVBPlayerRagdollComponent;
class UVBTargetLockComponent;
class UVBTraversalComponent;
class UVBInputConfig;
struct FInputActionValue;
enum class EVBWeaponType : uint8;

// 카메라 거리 단계. 값 이름은 GASP E_CameraStyle 과 같아야 한다 - 브리지가 이름으로 옮겨 적는다.
UENUM(BlueprintType)
enum class EVBCameraStyle : uint8
{
	Close,
	Medium,
	Far,
	Debug,
};

/**
 * 플레이어 캐릭터의 로컬 입력과 플레이어 카메라 고르기(Lyra ULyraHeroComponent): 입력 설정의 행동을 묶어 나눠 보내기, 손이 누른 이동값, GameplayCamera 켜고 끄기.
 * 맡지 않는 것: 서버 판정, 복제되는 의도(캐릭터), 키 배치(UVBInputDeviceManager 의 IMC), 카메라 부품 만들기, 처형 카메라.
 * 보내는 곳: 캐릭터의 의도 함수, 능력 입력(ASC), 기능 컴포넌트(각자의 FindFor).
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBHeroComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBHeroComponent();

	static UVBHeroComponent* FindFor(const AActor* Actor);

	// 능력 입력 하나를 ASC 에 보낸다(Lyra Input_AbilityInputTagPressed 의 자리). 벽에 붙은 동안은 이동 기술이 허락한 입력만 간다.
	//   서버의 공격 지연 길도 이 문을 지난다. PressSerial = 공격 누름 기록 번호(공격만).
	void DispatchAbilityInput(FGameplayTag InputTag, int32 PressSerial = INDEX_NONE);
	void DispatchAbilityInputReleased(FGameplayTag InputTag);

	// 캐릭터의 SetupPlayerInputComponent 가 부른다 - 묶음은 엔진이 회차마다 주는 입력 컴포넌트와 함께 사라진다.
	//   설정이 비거나 Native 줄이 빠지면 Warning 한 줄 - 조용히 아무 키도 안 먹는 상태가 되지 않게.
	void InitializePlayerInput(UInputComponent* PlayerInputComponent);
	// 캐릭터의 UnPossessed 가 부른다. 엔진은 입력 컴포넌트를 지우지만 키를 뗀 이벤트(Completed)를 보낸다는 보장은 없다 - 이동값과 기능의 이동 축을 0 으로.
	void EndPlayerInput();
	// 태그의 입력 행동(Native 목록 다음 Ability 목록). 상호작용 안내, 플레이 봇이 키를 찾는다. 설정이나 줄이 없으면 nullptr.
	const UInputAction* FindInputAction(const FGameplayTag& InputTag) const;
	const UVBInputConfig* GetInputConfig() const { return InputConfig; }
	// 손이 누른 이동값(이 로컬 입력 회차의 사실). 매달림, 상호작용처럼 이동이 CMC 로 안 가도 누르고 있다는 사실은 남는다.
	FVector2D GetRawMoveInput() const { return RawMoveInput; }
	// 그 액터의 영웅이 기록한 이동값. 영웅이 없는 캐릭터(적, 시험 캐릭터)는 0 - 바깥에서 읽는 곳은 모두 이것을 쓴다.
	static FVector2D GetRawMoveInputFor(const AActor* Actor);
	// 맞고 휘청이는 동안(State.Combat.HitReacting) 이동 입력은 버린다. 이동값 기록은 그 앞에서 한다.
	static bool ShouldBlockMoveForHitReact(const UAbilitySystemComponent* ASC);

	// 로컬 컨트롤러이고 카메라 자산이 있으면 GameplayCamera 를 켜고, 아니면 끈다.
	//   이미 켜졌으면 다시 켜지 않는다 - 뷰 대상이 캐릭터로 돌아와 처형 카메라를 덮는다.
	void RefreshActiveCamera();
	// 카메라 자산이 붙어 있나. 켜져서 평가 중인지와는 다르다.
	bool IsUsingGameplayCamera() const;
	// 카메라 리그가 브리지(VBGASPBridgeLibrary)로 읽는 거리 단계.
	UFUNCTION(BlueprintPure, Category="VB|Camera")
	EVBCameraStyle GetCameraStyle() const { return CameraStyle; }
	// 로컬 화면의 선택이라 복제하지 않는다.
	UFUNCTION(BlueprintCallable, Category="VB|Camera")
	void SetCameraStyle(EVBCameraStyle Style) { CameraStyle = Style; }

private:
	AVBCharacter* GetBody() const;

	// 프레임 입력을 받을 컴포넌트를 잡는다. 하나라도 없으면 조립이 깨진 것이라 에러를 남긴다.
	void CacheFrameTargets(AVBCharacter* Body);
	// 입력 설정의 Native 줄을 찾는다. 없으면 한 줄 남기고 null 이라 그 입력만 묶지 않는다.
	const UInputAction* RequireNativeAction(const FGameplayTag& Tag) const;
	// 이동, 시점, 점프를 묶는다.
	void BindMovementActions(UEnhancedInputComponent* Input, AVBCharacter* Body);
	// 공격, 앉기, 걷기 전환, 락온, 대상 전환을 묶는다.
	void BindCombatActions(UEnhancedInputComponent* Input, AVBCharacter* Body);
	// 숫자 키 칸을 묶는다.
	void BindWeaponSlotActions(UEnhancedInputComponent* Input);
	// 상호작용, 래그돌, 조준, 무기 소환을 묶는다.
	void BindUtilityActions(UEnhancedInputComponent* Input);
	// 입력 설정의 Ability 줄을 누름, 놓음 처리에 묶는다.
	void BindAbilityActions(UEnhancedInputComponent* Input);

	void HandleMove(const FInputActionValue& Value);
	void HandleMoveCompleted();
	void HandleLook(const FInputActionValue& Value);
	void HandleAttackPressed();
	void HandleAttackReleased();
	void HandleInteractPressed();
	void HandleInteractReleased();
	void HandleLockOnPressed();
	// 앉기 누름: 필살기 조합이면 필살기 입력, 아니면 캐릭터의 앉기, 미끄러지기.
	void HandleCrouchPressed();
	// 지금 앉기 누름이 필살기 조합이면 true. 질주 버튼, 장치, 이동 입력, 지금 걸린 키 묶음을 모아 VBKeyBinding 에 묻는다.
	bool IsSkillChordPress(const AVBCharacter& Body) const;
	// 앉기 놓음: 그 누름이 필살기였으면 필살기 놓음을 보낸다 - G 키 길과 같이 누름, 놓음이 짝을 이룬다.
	void HandleCrouchReleased();
	void HandleSwitchTarget(const FInputActionValue& Value);
	void HandleSwitchTargetReleased();
	void HandleWeaponSlot(int32 Slot);
	void HandleRagdollPressed();
	void HandleAimPressed();
	void HandleAimReleased();
	void HandleAimCanceled();
	bool IsAimToggle() const;
	// 능력 목록의 줄: 누름 = DispatchAbilityInput, 놓음 = DispatchAbilityInputReleased. 놓아서 끝날지는 능력이 정한다.
	void HandleAbilityPressed(FGameplayTag InputTag);
	void HandleAbilityReleased(FGameplayTag InputTag);

	UPROPERTY(EditDefaultsOnly, Category="VB|Input")
	TObjectPtr<const UVBInputConfig> InputConfig;

	// 기본값 Medium = GASP SandboxCharacter_CMC 와 같다.
	UPROPERTY(EditAnywhere, Category="VB|Camera")
	EVBCameraStyle CameraStyle = EVBCameraStyle::Medium;

	FVector2D RawMoveInput = FVector2D::ZeroVector;
	// 지금 누른 앉기 버튼이 필살기 조합으로 나갔는가 - 놓을 때 어느 쪽 놓음을 보낼지 가른다.
	bool bSkillChordHeld = false;

	// 매 프레임 오는 이동, 시점 입력을 받는 같은 캐릭터의 컴포넌트. 입력을 묶을 때마다 다시 받는다 - 빠진 것은 그 입력을 무시한다(묶을 때 Error 한 줄).
	struct FFrameInputTargets
	{
		TWeakObjectPtr<UVBLocomotionIntentComponent> LocomotionIntent;
		TWeakObjectPtr<UVBTargetLockComponent> TargetLock;
		TWeakObjectPtr<UVBInteractorComponent> Interactor;
		TWeakObjectPtr<UVBGASPSmartObjectComponent> SmartObject;
		TWeakObjectPtr<UVBPlayerRagdollComponent> Ragdoll;
		TWeakObjectPtr<UVBTraversalComponent> Traversal;
		bool IsComplete() const;
	};
	FFrameInputTargets FrameTargets;
};
