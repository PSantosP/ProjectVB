// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "VBInteractorComponent.generated.h"

class UVBInteractableComponent;
struct FVBInteractionMotion;
class UVBPushableComponent;

// 대상과 동작 번호를 한 번에 복제해 서로 다른 상호작용의 조합을 읽지 않는다.
USTRUCT()
struct FVBEnvironmentMotionState
{
	GENERATED_BODY()
	UPROPERTY()
	TObjectPtr<UVBInteractableComponent> Target;
	UPROPERTY()
	uint8 MotionIndex = MAX_uint8;
};

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FVBInteractFocusChangedSignature, UVBInteractableComponent*, NewFocus);
DECLARE_MULTICAST_DELEGATE_OneParam(FVBOnInteractionRefused, const FText& /*RefusedText*/);

/**
 * 플레이어 쪽 상호작용. 가리키는 대상을 고르고 홀드 진행을 프롬프트에 보여준다.
 * 서버와 클라가 같은 FindBestInteractable 을 부른다 - 클라는 매 틱 초점 표시용, 서버는 GA 활성화 때 판정용.
 * 클라가 고른 대상은 서버로 보내지 않는다(서버가 어차피 다시 재야 한다). 판정은 GA 가 담당한다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBInteractorComponent : public UActorComponent
{
	GENERATED_BODY()

	// 프롬프트 수명 시험이 초점을 정한다(SetFocus).
	friend class FVBHUDRowTestAccess;

public:
	UVBInteractorComponent();
	// AVBCharacter 가 만든다. 없으면 환경 상호작용을 하지 않는 액터다.
	static UVBInteractorComponent* FindFor(const AActor* Actor);

	// 지금 이 자리에서 닿는 대상 가운데 카메라 방향에 가장 가까운 것. 없으면 null.
	UVBInteractableComponent* FindBestInteractable() const;

	// 이 대상을 지금 고를 수 있는가(거리, 높이, 카메라 방향, 시야).
	bool CanReach(const UVBInteractableComponent* Interactable) const;

	// 하고 있는 상호작용을 이어 가도 되는가(손이 닿는 시각, 홀드 완료). 고른 뒤 카메라를 돌렸다고 취소되지 않게 방향은 안 본다.
	bool CanKeep(const UVBInteractableComponent* Interactable) const;

	UVBInteractableComponent* GetFocusedInteractable() const { return FocusedInteractable.Get(); }

	// 홀드 표시. UVBGA_Interact 가 로컬 인스턴스에서 부른다.
	void BeginHoldDisplay(float Duration);
	void EndHoldDisplay();

	// 0~1. 홀드 중이 아니면 음수.
	float GetHoldProgress() const;

	// 캐릭터 동작이 있는 상호작용 동안 캡슐 방향을 제어한다.
	// 몽타주가 제자리라 그 사이 strafe 회전이 캡슐을 돌리면 소품을 등지고 동작한다.
	bool OwnsCapsuleFacing() const;

	// UVBGA_Interact 가 캐릭터 동작 시작, 끝에 부른다. 소유자의 회전 모드를 다시 평가시킨다.
	void BeginMotion(UVBInteractableComponent* Target, uint8 MotionIndex);
	void EndMotion();
	UVBInteractableComponent* GetMotionTarget() const { return MotionState.Target; }
	const FVBInteractionMotion* GetActiveMotion() const;
	uint8 GetMotionIndex() const { return MotionState.MotionIndex; }
	UVBPushableComponent* GetPushable() const;
	void SetPushInput(float Value);
	void RequestRepeatInteraction();

	// 초점이 바뀌었다. 로컬에서만 온다(상호작용 프롬프트가 구독).
	UPROPERTY(BlueprintAssignable, Category="VB|Interaction")
	FVBInteractFocusChangedSignature OnFocusChanged;

	// 서버가 시도를 대상의 조건 칸으로 거절했다. UVBGA_Interact 가 서버에서 부르고 소유 클라이언트에서 돈다.
	UFUNCTION(Client, Reliable)
	void ClientInteractionRefused(const FText& RefusedText);

	// 위 거절이 소유 클라이언트에 왔다. HUD 알림 줄이 구독한다.
	FVBOnInteractionRefused OnInteractionRefused;

protected:
	virtual void BeginPlay() override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	// CanReach, CanKeep 의 공통 구현. 다른 것은 카메라 방향을 보느냐뿐이다.
	bool Reaches(const UVBInteractableComponent* Interactable, bool bCheckFacing) const;

	// 소유 폰이 이 기계에서 조종되는가. 초점은 그 경우에만 고른다 - 서버의 남의 폰이나 시뮬레이트 프록시는 프롬프트가 없다.
	bool IsLocallyControlledOwner() const;

	void SetFocus(UVBInteractableComponent* NewFocus);

	// 서버: 자동 대상의 범위 출입을 보고, 들어오는 순간 한 번 완료를 알린다.
	void UpdateAutoInteractables();

	// 지금 범위 안에 있는 자동 대상. 나갔다 들어와야 다시 걸린다.
	TSet<TWeakObjectPtr<UVBInteractableComponent>> AutoInside;

	TWeakObjectPtr<UVBInteractableComponent> FocusedInteractable;

	// 홀드 표시 상태. 음수 = 홀드 중 아님.
	double HoldStartTime = -1.0;
	float HoldDuration = 0.0f;

	UPROPERTY(ReplicatedUsing=OnRep_MotionState)
	FVBEnvironmentMotionState MotionState;
	UFUNCTION()
	void OnRep_MotionState();
	UFUNCTION(Server, Reliable)
	void ServerRepeatInteraction();
	UFUNCTION(Server, Unreliable)
	void ServerPushInput(float Value);
	float LocalPushInput = 0.0f;
	double NextPushInputTime = 0.0;
};
