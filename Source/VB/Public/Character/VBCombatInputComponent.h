// ProjectVB Copyright (c) 2025. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Engine/EngineTypes.h"
#include "VBCombatInputComponent.generated.h"

class UVBAbilitySystemComponent;
class ACharacter;
struct FGameplayTag;

/**
 * 공격, 가드 누름이 서버에서 무엇이 되나를 맡는다: 로컬 반(누름 번호, 방향), 서버 입구 RPC, 판정(콤보, 저장, 일어선 뒤, 새 활성), 칼집 상태 누름의 발도, 회피 커밋, 일어서기 지연.
 * 맡지 않는 것: 콤보 섹션, 창(공격 능력), 누름 기록(ASC), 능력 입력의 벽 자세 문(영웅), 남은 이동 입력(캐릭터).
 * 보내는 곳: 능력(ASC), 발도는 무기 상태. RPC 는 누름, 놓음 순서가 지켜지게 같은 액터 채널의 reliable 이다. BP_VBCharacter 가 붙이고 FindFor 로 찾는다.
 */
UCLASS(ClassGroup=(VB), meta=(BlueprintSpawnableComponent))
class VB_API UVBCombatInputComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UVBCombatInputComponent();

	static UVBCombatInputComponent* FindFor(const AActor* Actor);

	// 로컬 반 - 영웅의 입력 콜백이 부른다. RawMoveInput = 누를 때 손이 누른 이동 입력(카메라 기준).
	void LocalAttackPressed(FVector2D RawMoveInput);
	void LocalAttackReleased();
	// 로컬 반 - 가드. 누름, 놓음을 번호와 함께 서버로 보낸다(켜고 끄는 것은 서버, 결과는 능력 복제로 온다).
	void LocalGuardPressed();
	void LocalGuardReleased();

	// 서버. 받아들인 마지막 공격 누름의 방향(월드 XY)을 한 번 내주고 비운다. 누른 단계(첫 타, 누른 다음 타)만 부른다.
	FVector2D ConsumeAttackIntent();

	// 회피 커밋(서버): DodgeCommit 태그(개수 1 고정)로 경공격을 막고 CancelSeconds 뒤 풀어 BufferSeconds 안의 저장 누름을 내보낸다.
	// 타이머가 여기인 것은 EndAbility 가 능력의 타이머를 지우기 때문이다.
	void BeginDodgeCommit(float CancelSeconds, float BufferSeconds);
	// 회피가 취소(피격)로 끝났을 때: 커밋과 저장 누름을 함께 버린다(서버).
	void CancelDodgeCommit();
	/** 공중 공격 종료 뒤 착지까지 공격 시동을 제한 */
	void BeginAirRecovery();

	// 캐릭터가 일어섰다(OnEndCrouch, 서버). 웅크림 때문에 미뤄 둔 누름이 BufferSeconds 안이면 내보낸다.
	void HandleStoodUp(float BufferSeconds);

	// 이 캐릭터의 공격 입력 상태를 비운다(연결 해제, 사망, 멱등) - 회피 커밋 태그는 받은 그 세션의 ASC 에서만 내리고 다시 찾지 않는다.
	void ResetForAvatarSession(UVBAbilitySystemComponent* SessionAbilitySystem);

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	// 서버 입구: 태그가 아니라 누름 번호와 누를 때의 월드 이동 방향만 받는다(태그를 받으면 클라가 서버 판정 사건을 쏠 수 있다). Validate 는 늘 true - false 는 연결 끊기다.
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerAttackPressed(int32 PressSerial, FVector2D MoveIntent);
	UFUNCTION(Server, Reliable)
	void ServerAttackReleased(int32 PressSerial);
	UFUNCTION(Server, Reliable, WithValidation)
	void ServerGuardPressed(int32 PressSerial);
	UFUNCTION(Server, Reliable)
	void ServerGuardReleased(int32 PressSerial);

	UVBAbilitySystemComponent* GetAbilitySystem() const;
	// 새 요청의 문: 살아 있고 이 캐릭터가 ASC 의 지금 아바타일 때만 - 죽은 뒤, 옛 아바타로 늦게 온 요청은 새 누름이 아니다.
	bool AcceptsRequests() const;
	void DispatchPress(int32 PressSerial);
	// 서버. 이 누름을 능력에 보낸다 - 손에 무기가 없으면 먼저 꺼내고, 가진 무기가 없으면 실패 알림으로 끝난다.
	void DispatchToAbilities(const FGameplayTag& InputTag, int32 PressSerial);
	void EndDodgeCommit(float BufferSeconds);
	UFUNCTION()
	void OnRecoveryLanded(const FHitResult& Hit);
	UFUNCTION()
	void OnRecoveryMovementChanged(ACharacter* Character, EMovementMode PreviousMode, uint8 PreviousCustomMode);
	void ClearAirRecovery();
	// 저장된 누름의 나이(초). 기록이 없으면 음수 - 버린다.
	double PressAge(int32 PressSerial) const;

	FVector2D PendingIntent = FVector2D::ZeroVector;
	int32 PendingDodgePress = INDEX_NONE;
	int32 PendingStandPress = INDEX_NONE;
	FTimerHandle DodgeCommitTimer;
	TWeakObjectPtr<UVBAbilitySystemComponent> AirRecoveryAbilitySystem;
};
